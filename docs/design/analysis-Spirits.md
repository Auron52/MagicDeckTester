# Analysis ledger — Spirits

Deck: `decks/Spirits/Spirits.cod` (60, W/U(/b) Spirits tribal on Aether Vial: Drogskol Captain + Supreme
Phantom lords, Mausoleum Wanderer, Patrician Geist, Geist of Saint Traft, Lingering Souls (black
flashback off Godless Shrine / Watery Grave / Flooded Strand), Skyclave Apparition, Usher of the Fallen,
Metallic Mimic, tribal lands Unclaimed Territory + Secluded Courtyard, Azorius Chancery; no sideboard, so
no wish reach).
Started 2026-11-13 on branch `phase-1-2-deck-analyzer` (base afd791934, `HEAD:src` b22177c9d310).

## Stage 1 — coverage (2026-11-13)

Already implemented (9): Plains, Island, Godless Shrine, Watery Grave, Aether Vial, Metallic Mimic,
Unclaimed Territory, Secluded Courtyard, Azorius Chancery.

Missing (14): **Geist of Saint Traft, Drogskol Captain, Supreme Phantom, Spirit of the Labyrinth,
Skyclave Apparition, Lingering Souls, Flooded Strand, Hallowed Fountain, Millicent Restless Revenant,
Patrician Geist, Mausoleum Wanderer, Shacklegeist, Usher of the Fallen, White Orchid Phantom.**

`fetch_hold_review.required=false` at Stage 1 only because Flooded Strand is not in `cards.json` yet;
re-run at Stage 3 — it becomes required once the Strand exists.

## Stage 2 — cards

Research fan-out (2026-11-13): five Opus agents, read-only, grouped by mechanic; drafts in
`logs/spirits/drafts/` (gitignored scratch; the table below is the durable record). Integration serial.

| card | tier | model | viewer bucket | status |
|---|---|---|---|---|
| Geist of Saint Traft | 2 | Hero-of-Bladehold attack token (self-attack only, flat 1), 4/4 white flying Angel (`attack_token_keywords` + `created_token_color`), new `attack_token_exile_end_of_combat`: FireAttackCreateTokens reports the token numbers, shared `ExileEndOfCombatTokens` at EndCombat in BOTH executor (GameEngine::CombatPhase) and rollout (SimulateOneCombatAfterTitan) | none (legend_keep for a 2nd copy) | IMPLEMENTED 87078b215 |
| Millicent, Restless Revenant | 2 | `affinity_for_subtype` Spirit + generic AFFINITY cast-order tier 25 + same-turn credit for Vial puts / token-making hand casts / a Spirit-named Mimic (`AffinityFeedCount`); new `combat_damage_tokens_per_trigger` + `combat_damage_watch_nontoken` (shared FireCombatDamageTokens); new `dies_watch_nontoken` (OnCreatureDies both walks, DeathOfWouldPay) | none (legend_keep for a 2nd copy) | IMPLEMENTED 55c43ab8a |
| Usher of the Fallen | 3 | first BOAST: `pay_token_cost {1}{W}` + new `activate_only_if_attacked_this_turn` + `activate_once_each_turn`; a post-combat main opened only when a boast is live | A (main-2 plan action) | IMPLEMENTED 9176533f0 |
| Lingering Souls | 2 | `cast_creates_tokens 2` Spirit fliers + `flashback_cost {1}{B}`; FIXED Hole A (flashback in DeckGraveyardReaders / BuildSimKey / Dominance; moves Gods) + Hole B (per-copy + PENDING flashback candidates, `SubsetHasNonCanonicalGyCopies`); shared `GraveyardCastSpellCost`; executor cast-count drift fixed | A (`flashback_gy`, `main_phase` plans incl. hand cast + flashback) | IMPLEMENTED 8097213a2 |
| Spirit of the Labyrinth | 3 | 3/1 body (87078b215); per-turn draw cap `each_player_draw_cap_per_turn` MODELLED in both worlds (core/DrawCap.h, choke point AttemptDrawIntoHand; integrator F, USER ruling Q1) | none (a rule, not a choice) | IMPLEMENTED 7421628b0 |
| Skyclave Apparition | 2 | new `etb_exile_opp_nonland_nontoken_max_mv` 4 (ResolveEtbExileUpToOne, shared enter cascade: cast + Vial put) + `ltb_exiled_owner_illusion_token` (permanent exile on a NEW link range `kSkyclaveExileBase`, never returned by the Cathar orphan sweep); target = provider identity fold `EtbOptionalExileTargetPick` (largest) | A (`dig` "(may exile)") | IMPLEMENTED 3ddc3c86f |
| Shacklegeist | 1 | vanilla 2/2 flier (NO flash per live oracle); tap ability / block restriction PROVISIONAL inert | none | IMPLEMENTED 87078b215 |
| White Orchid Phantom | 2 | new `etb_destroy_nonbasic_land_optional` (ResolveEtbDestroyNonbasicLandOptional, both sides); own lands SEARCHED (cast AND Vial-put variants pinned on tutor_target, `#WO`, the basic a land_search_pins ordinal); base = provider fold (opp nonbasic, else none); opponent gets a tapped `Opponent Basic Land`; Spirits prunes own variants with no basic in library | A (`dig` "(may destroy nonbasic land)" + `tutor_etb`) | IMPLEMENTED 3ddc3c86f |
| Flooded Strand | 1 | Arid Mesa battlefield-fetch schema, Plains/Island; routed to SpiritsProvider (lossless fetch fan, hold kept); a HELD Strand may be cracked first to fund a same-main cast (E1, crack-first plans) | A (fetch, hold, shock) | IMPLEMENTED 87078b215; held-fetch hole FIXED ea8d143e9 |
| Hallowed Fountain | 1 | Steam Vents schema | A (`land_entry`) | IMPLEMENTED 87078b215 |
| Drogskol Captain | 1 | `lord_effect` Spirit +1/+1, `lord_excludes_self`; hexproof grant PROVISIONAL inert | none | IMPLEMENTED 87078b215 |
| Supreme Phantom | 1 | `lord_effect` Spirit +1/+1, excludes self | none | IMPLEMENTED 87078b215 |
| Patrician Geist | 2 | Spirit lord + new `reduces_graveyard_cast_generic 1` inside the shared `GraveyardCastSpellCost` (Souls flashback {1}{B}→{B}; executor Lurrus cast hooked too) + same-turn credit (`SameTurnReducerCounts::gy_cast`, any_reducer, ManaPruneBound bail, gate soft term) for a Geist cast/Vialed in the same plan; NOT `reduces_spell_subtype` (→ goblin route) | none | IMPLEMENTED 83ff5dedc |
| Mausoleum Wanderer | 2 | +1/+1 UNTIL EOT (not a counter) when another Spirit enters: new `own_creature_enters_self_temp_power/_tough` + `enters_watch_subtypes Spirit` (FireCreatureEnterWatchers, `DefHasCreatureEnterWatcher`, `DuplicateEntryOrDeathHasUpside`, 5th `spirits` routing term); sac-to-counter PROVISIONAL deferral | none | IMPLEMENTED 83ff5dedc |

Integration plan (serial, one agent each, brief `logs/spirits/integrator-common.md`):
A = SpiritsProvider routing + Captain, Phantom, Shacklegeist, Labyrinth, Strand, Fountain, Geist of Saint Traft;
B = Lingering Souls + flashback Holes A/B + Patrician Geist discount;
C = Mausoleum Wanderer, Millicent, Skyclave Apparition, White Orchid Phantom;
D = Usher of the Fallen (boast, post-combat main).

Scryfall note: concurrent research agents 429'd the single-card API; texts came via one `/cards/collection` POST
(`logs/spirits/drafts/scry/`) and web pages. The cost + field audits (2d-bis) are the authoritative re-check.

### Open questions (USER-owned marked)
- ~~**[USER, PROVISIONAL default taken]** Spirit of the Labyrinth's draw cap deferred as inert~~ -- USER rejected the deferral (Q1); MODELLED by integrator F, 7421628b0.
- **[USER, PROVISIONAL]** Skyclave Apparition's exile target folded to one default (opponent's largest creature, Brutal Cathar convention) in autonomous play — all choices outcome-equivalent vs the goldfish; the human sees all.
- **[USER, PROVISIONAL]** Shacklegeist's tap ability + blocking restriction, flying (no blocking anywhere) deferred as inert.
- **[USER, PROVISIONAL]** White Orchid Phantom: the opponent's "may search for a basic" is modelled as an `Opponent Basic Land` placeholder entering tapped (integrator C2 took the draft's faithful option instead of the Path no-op; nothing reads it).
- **[USER]** Fetch hold policy for Flooded Strand — measure first with full timing (provider returns every distinct target, hold offered), then A/B named policies.
- **[USER, PROVISIONAL]** Drogskol Captain's hexproof grant deferred as inert (the opponent never targets; Lord of Atlantis islandwalk precedent, approved 2026-11-01).
- **[USER, PROVISIONAL]** Mausoleum Wanderer's "sacrifice: counter target instant/sorcery unless its controller pays {X}" deferred. Only target is our own Lingering Souls; it can beat keeping Wanderer only with Millicent on the battlefield (sac → nontoken Spirit dies → 1/1 token), a narrow window disclosed here. Tier-3 alternative: a Remand-style pay-branch rider.
- **[USER]** Fast playouts / d0 may under-rank lords (`EvalCard` scores a lord by its own body). The search's combat applies the bonus correctly; a cast-order hook would be a user call.
- Flash is inert engine-wide (no off-main-phase casting window); weakly dominated vs a passive goldfish.
- **[USER, PROVISIONAL]** Usher of the Fallen's boast lives in SpiritsProvider's narrow boast-only post-combat main (main 1 vs main 2 is the user's per-deck call; the boast is illegal before attackers, so this window is its only reach; no flag). Alternative: a full second main (`uses_second_main`). See Integrator D.

### Integrator A (2026-11-13): SpiritsProvider + seven cards

**Routing (Stage 4a, done before any card landed).** Probe with only the nine pre-existing cards:
`provider=Vial` (Aether Vial's `upkeep_adds_charge`). New `SpiritsProvider : DeckProvider`, routed by a
`spirits` signature returned right after `shadowborn` (above `anti`, `goblin`, `vial && slivers`). Terms,
OR'd across four cards: a lord whose `subtypes_affected` names "Spirit" (Captain, Phantom, later Patrician
Geist); `attack_token_exile_end_of_combat` on a Spirit (Geist); `affinity_for_subtype` on a Spirit
(Millicent -- integrator C: keep that param on her and this term routes; `dies_watch_subtype` would set
`goblin`, but `spirits` returns first). Excluded: Vial / Mimic / tribal lands (shared tribal staples),
Spirit of the Labyrinth's cap (a hatebear any W list adds), fetch/shock lands, Lingering Souls' token /
flashback params. Pinned in `test_pirates_provider.cpp`'s every-deck table plus a cut-one-card test.
- **Base = DeckProvider, not VialProvider (agent call, surfaced).** VialProvider's only non-root hooks are
  the Knights cast-order clauses (no Spirits card carries their params -> inert) and slivers_vial's
  USER-settled discard buckets, which the routing chain says a new Vial deck must not inherit (the Vial
  charge policy is the root default). Kithkin / Merfolk / Pirates precedent. The deck had never been
  measured on either.
- **Fetch surface (lossless).** `FetchCandidates` = every distinct legal target; `FetchSearchCandidates`
  = the whole list (NOT the top-1 default). `ConsiderFetchHold` = root default (true), so the Strand is
  never collapsed on play: it is held, and the later SacLandSearch activation's pick is itself searched
  (LandSearchPicks). The FetchSearchCandidates widening is therefore inert today; it only keeps the
  provider lossless if a hold policy is ever adopted. **[USER]** the fetch-target / hold policy is still
  yours: measure the full-timing default first, then A/B a named policy (draft 04 section 3).
- **Engine fix (completeness): crack-on-play BESIDE the hold.** With the hold kept, the Strand was only ever
  played as a permanent and its activation ran in the TRAILING pass after every cast, so "play Strand, crack
  for an Island, cast Drogskol Captain" was unfindable at `--budget-ms 0 --depth 5` (win T5, expected T4).
  New `FetchCrackOnPlayOffered` / `FetchCrackOnPlayPinned` (DecisionProviders.cpp): the enumerator (hand land
  and Courser library-top land) emits one crack-on-play variant per searched target with a PINNED
  `fetch_target`, and `PlayLandCore` collapses a pinned modern fetch. Byte-identical for every other deck
  (Burn / FiveColour / Anti decline the hold so FetchResolvesOnPlay is already true; legacy fetches are
  immediate). NOT in human play (viewer menu indices are what references replay by) -- a viewer surface for
  the same line is OPEN. Pinned by `test/scenarios/spirits_strand_fetches_the_blue_source.json`.
- **OPEN engine hole (FIXED by integrator E1, ea8d143e9 -- see below):** a Strand already HELD on the battlefield cannot be cracked to pay for a
  same-main cast (casts are priced before the trailing crack). Biases any full-timing vs hold measurement
  against the hold. `docs/design/held-fetch-crack-then-cast-hole.md` (repro in `logs/spirits/repro/`).
- Certificate: NotAssessed (Vial instant puts, lords on tokens/Mimic, Geist's Angel, Wanderer pumps, Usher boast).

**Cards.** Geist of Saint Traft (Tier 2): new `attack_token_keywords`, `attack_token_exile_end_of_combat`
(CardParams + loader); `FireAttackCreateTokens` gains an optional out-list of eoc token numbers and now
passes the source's `created_token_color` / keywords (both empty for Adeline / Hero -> byte-identical);
`ExileEndOfCombatTokens` (SpellEffects.h) is called at `Step::EndCombat` in GameEngine::CombatPhase and
after combat damage in SimulateOneCombatAfterTitan. No new state field (the list is local to combat).
Spirit of the Labyrinth: `each_player_draw_cap_per_turn` + bitfield deck constant `deck_has_draw_cap`
(StampDeckTraits, main+side; sizeof(GameState) unchanged); `handentry::NoteFirstDraw` aborts on a second
Draw/DrawStep entry in a turn with a cap permanent out (reuses `first_draw_card_number_this_turn`, which is
folded only for miracle decks; `pending_miracle_card_number` is now set only for miracle decks).
Captain / Phantom / Shacklegeist / Strand / Fountain: cards.json only.
**Deviation from draft 04:** Shacklegeist's bracket note said Wanderer gives "entering-Spirit counters"; it
is an until-EOT pump (draft 01) -- not repeated in the note. Draft 04's colour-need fetch ranking NOT
encoded (judgement hook; brief: lossless).

**Verification (2026-11-13, integrator A).** Unit suite 2866/2866 (new `test/unit/test_spirits_cards.cpp`:
routing + cut-one-card, lossless fetch fan + crack-on-play offer, lords incl. self-exclusion and the
non-Spirit Angel, Geist 2+4 then exiled at end of combat / sick Geist makes none / flat count in 2HG / Hero's
tokens untouched, Labyrinth guard's non-aborting paths); scenarios 495/495 (three new `spirits_*`);
`provider_audit.py --check` rc 0, every deck's routing unchanged; `audit_card_fields.py` rc 0 (snapshot
updated for the seven cards); cost audit compared 4/5 costed cards (Drogskol Captain HTTP 429 -- verified by
hand against the Scryfall collection JSON: `{1}{W}{U}`, cmc 3). Smoke: 194 pass, 1 FAIL
`extraturns_smoke_d3_s1001` (same score 4.7200, gi28 digest moved) -- NOT this change: re-running that job
twice in isolation with the same binary reproduces the GT digest 76322f02352e42d8 exactly (units 417966 vs
440412 in the smoke run, which shared the box with the scenario suite for its first minutes). play-changed
attributable to this change: 0.

**Provisional deferrals (USER sign-off owed):** Drogskol Captain's hexproof grant; Shacklegeist's tap
ability, block restriction (and Flying, inert everywhere); Spirit of the Labyrinth's draw cap (guarded).

### Integrator B (2026-11-13): Lingering Souls + the flashback holes

**Card (Tier 2, JSON on existing params).** `cast_creates_tokens 2` (1/1 white Spirit, Flying via
`cast_created_token_keywords`) + `flashback_cost {1}{B}`, cost/oracle verbatim from live Scryfall
(`logs/spirits/drafts/scry/souls/`; snapshot row added to `scryfall_reference.json`). The tokens carry the
Spirit subtype on their own Card: Supreme Phantom makes them 2/2 (unit test).

**Hole A (soundness), all three draft sites verified and fixed.** `DeckGraveyardReaders` (GoldFishRunner.cpp),
BuildSimKey's graveyard fold (`gy_retraceable`, TurnSolver.cpp) and Dominance.h's named projection keyed only
`retrace`; they now treat `flashback_cost` the same (the bit `GyR_RetraceNames` = "castable from the
graveyard"). "Souls still in the graveyard" and "already flashed back" no longer share a TT / memo key or
compare equal in EOT dominance. **Moves the Gods key** (Faithless Looting, Mystic Retrieval).
Not fixed, surfaced: `tutor_from_graveyard` (Mystic Retrieval, Reborn Hope, Auroral Procession) has NO reader
bit either -- a regrowth's target pool is a graveyard reader the stamp does not see (Gods is now partly covered,
since any flashback card in its graveyard folds the whole graveyard).

**Hole B (completeness).** Verified unfindable at HEAD by two new scenarios at `--depth 3 --budget-ms 0`
(`spirits_souls_cast_then_flashback_same_main`, `spirits_souls_double_flashback_same_main`: both read win T6,
the line wins T5). Fix, an ENUMERATOR fix (the USER's Astrolabe ruling: no new breakpoint):
- B2: CollectActions emits a flashback once per graveyard COPY (retrace stays one per name).
- B1: a post-pass (after every pass that can drop a hand cast) emits one PENDING `CastFromGraveyard` per hand
  copy of a flashback card that has a hand cast in the list. Both worlds already apply every graveyard cast
  after the hand casts (ApplyPlanDirect's trailing pass; AIEngine::TakeTurn), so the card is there. A regrowth
  flashback's pending cast needs >= 2 targets across graveyard + other hand cards (superset).
- `SubsetHasNonCanonicalGyCopies` (both enumerators: Solve::consider and EnumeratePlans' eval_and_push, built in
  SubsetFilterPre on both paths): rejects k flashbacks of a name > graveyard copies + selected hand casts (the
  surplus finds no card: an identity fold), and, under the odometer fold only, a non-prefix of the identical
  copies (k copies cost k+1 subsets). Plan signatures key a graveyard cast by name in both builders (no new
  marker; `check_plan_sig_lockstep.py` output unchanged: only the pre-existing `#BB`). No cap, no greedy pick;
  inert for every deck without `flashback_cost`. A copy that reaches the graveyard another way mid-plan (a
  Looting discarding a Looting) is reached through the draw breakpoint's continuation, as before.

**Shared price (for the next integrator).** `GraveyardCastSpellCost(def, state)` (ManaPayment.h/.cpp) is now THE
graveyard-cast price at every site: CollectActions' retrace/flashback block, the pending post-pass, the Lurrus
block and its site-9 key, the rollout's apply_one and the executor's `cast_from_graveyard`. **Patrician Geist's
"{1} less from your graveyard" goes inside it** (generic only, floor 0). Caveat: the executor's LURRUS cast goes
through `cast_by_name` (hand pricing) and would need the same hook if a Lurrus deck ever plays a Geist.

**Executor drift fixed (found by the B1 unit test).** The executor's `cast_from_graveyard` never counted the cast
(`spells_cast_this_turn`, `mv_cast_this_turn`, the main-2 stager stamp); the rollout's apply_one always did.
Every retrace / flashback cast was affected (Gods; TreasureHunt's Throes of Chaos). Log label now "(flashback)".

**Viewer (bucket A, nothing new to wire).** `--claude-play` confirmed: seed 8, choices
`1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0` (T5) offers `Aether Vial, Lingering Souls (flashback), Lingering Souls
(flashback)` and `Lingering Souls, Lingering Souls (flashback)`; seed 49, choices
`1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0` (T6, graveyard empty) offers plan 0 `land=Plains; cast: Lingering
Souls, Lingering Souls (flashback)`, and choosing it makes four tokens with Souls exiled, no drop.
**OPEN (human-play payment, not a search hole):** seed 8 T5 plan 5 (`land=Godless Shrine`, shock paid) drops the
flashback: the hand Souls' {2}{W} is paid with the Shrine instead of an Unclaimed Territory, stranding {B}. The
autonomous apply pays the same board (scenario variants with Chancery + Territories pass at b0).

**Verification.** New `test/unit/test_spirits_souls.cpp` (8 cases: card + lord, shared price, stamp, dominance,
B2 per-copy + prefix + both worlds, B1 pending + both worlds, 1 gy + 1 hand, inert deck); `test_gods_coliseum`'s
Retrieval count updated (the hand copy's pending flashback is a legal 7-mana line). Units 2874/2874; scenarios
497/497; `gods_plan_menu_check.py` OK; `audit_card_fields.py` rc 0; `provider_audit.py --check` rc 0;
`audit_viewer_decisions.py --no-sweep` rc 0.

**Smoke (2026-11-13, sequential, alone on the box):** 191 pass, 4 FAIL -- ALL Gods, the expected Hole A/B +
executor-count movement, a deliberate correctness change: `gods_smoke_d0` 8.7860 -> 8.7830, `gods_smoke_d3`
5.4000 -> 5.4000 (digest), `gods_smoke_d5` 5.2200 -> 5.2000, `gods2hg_smoke_d5` 5.4000 -> 5.3200. Searched:
slower 1 / faster 3 / play-changed 79; d0: slower 9 / faster 12. The one searched slower game,
`gods_smoke_d3_s1001` gi19 (T8 -> unwon at d3 b10), wins **T6 at --depth 5 and --depth 8 --budget-ms 0**
(budget churn). Every other deck byte-identical. Not accepted (the orchestrator batches GT accepts).
(`test/explain_game.py` cannot replay Gods: "Unknown keyword: Companion" -- pre-existing.)

**Open (surfaced, not decided):** (1) the human-play payment drop above; (2) `tutor_from_graveyard` has no
graveyard-reader bit (Hole A's sibling); (3) retrace stays one candidate per NAME, so "retrace Throes twice in
one main" (two land discards) is still unenumerable -- the same shape as B2, not in this scope.

### Integrator B2 (2026-11-13): Patrician Geist + Mausoleum Wanderer

**Patrician Geist (Tier 2).** Spirit lord on existing params plus NEW `reduces_graveyard_cast_generic`
(CardParams + loader; NOT `reduces_spell_subtype`, which is a Goblins signature term). Applied inside the shared
`GraveyardCastSpellCost` (ManaPayment.cpp: new `GraveyardCastGenericDiscount` = sum over the caster's permanents,
generic only, one more floored reduction = CR 601.2f's single floor), so every graveyard-cast price/pay site reads
it: CollectActions' flashback/retrace block, the B1 pending post-pass, the Lurrus block + its site-9 key, the
rollout's apply_one, the executor's `cast_from_graveyard`, AND (new) the executor's LURRUS cast
(`CastSpellFromHand` with `gy_cast_source_id >= 0` now prices through the helper, as apply_one already did;
identical for every Lurrus card when no Geist is out).
**Same-turn completeness** (a Geist cast or Vial-put earlier in the plan discounts the plan's flashback), every
gate the draft named, checked against the code:
- `SameTurnReducerCounts::gy_cast` (CountSameTurnReducers) + `SameTurnReducerDiscountFor` credits each
  CastFromGraveyard cand; `ActionGyCastReducerAmount` counts a hand CAST and a Vial PUT (Vial actions carry no
  def; looked up by name, the Ragemonger shape). EXACT, not optimistic: both apply worlds resolve every graveyard
  cast after the hand casts and Vial puts (verified in both worlds by the unit tests below).
- EnumeratePlans' `any_reducer` arms that credit when a Geist cand AND a graveyard-cast cand coexist.
- `ManaPruneBound` bails under the same condition (the MTG_SUBRED_BAIL precedent).
- Selection-exact gate: a Geist term is `block` + `soft` with `red = 1` (an upper bound: SelGateExactRefuses
  counts it against every selected cast); `ManaGateWouldHelp` sees it. A Vial-put Geist (no def) blocks hard.
- The cost-reframe `interacts` test (default OFF) sees it too. Not touched: Solve's leaf credit
  (`MTG_LEAF_REDUCER_CREDIT`, default OFF for every reducer: beyond-horizon policy), and the ef rescue's
  sequential walk (refuses graveyard casts outright, "unmodelled kind").
- Proof: `test/scenarios/spirits_geist_discounts_same_main_flashback.json` at `--depth 3 --budget-ms 0`: win T5
  (Geist {2}{U} + flashback {B} on four lands is the only T5 kill); with `MTG_NO_COST_TRICKS=1` (credit off) it
  reads **T6** -- the line is unfindable without the credit. Unit: same line + the Vial-put form (Vial(3) Geist +
  {B} on ONE land) enumerated and paid identically by ApplyPlan and AIEngine::TakeTurn; three lands correctly
  admit no Geist + flashback plan.
**Mausoleum Wanderer (Tier 2 + PROVISIONAL deferral).** NEW `own_creature_enters_self_temp_power/_tough` + existing
`enters_watch_subtypes ["Spirit"]`: a FireCreatureEnterWatchers clause (the Valkyrie filter, landfall-pump
payload into temp_power_bonus/temp_tough_bonus, cleared at both cleanup sites; already in BuildSimKey). Added
to `DefHasCreatureEnterWatcher` (else silently skipped) and to `DuplicateEntryOrDeathHasUpside` (a duplicate
legendary Spirit dying to the legend rule still pumps). Verified against the live Scryfall JSON: +1/+1 UNTIL
END OF TURN, not a counter. Fires for tokens (Souls), Vial puts and casts in both worlds (unit), and through
combat (`spirits_wanderer_pumped_by_souls_tokens.json`: Souls pre-combat makes the Wanderer a 3/3, kill T4).
Fifth `spirits` routing term (a Spirit carrying the pump and watching "Spirit"); the cut-one-card test now cuts
each of the five cards. Sac-to-counter: **[USER, PROVISIONAL]** deferred with the bracket note (open question
above, unchanged).
**Viewer:** no new decision (static discount; mandatory untargeted pump). INERT_PARAMS += the three params.
**Lingering Souls' oracle note** lost its "[PARTIAL: Patrician Geist ... NOT applied yet]" (discharged).
**Not done (only matters if Spirits ever gets a second main):** ClassifyMainPhase has no dependency pull of
Spirit casts to Main1 while a Wanderer is out (draft 01's note). Spirits is single-main today.
**Verification (2026-11-13, integrator B2).** New `test/unit/test_spirits_geist_wanderer.cpp` (11 cases); routing
cut-one-card test extended to Patrician Geist and Wanderer. Units 2885/2885; scenarios 499/499 (two new);
`audit_card_fields.py` rc 0 (snapshot rows added from the Scryfall collection JSON); `provider_audit.py --check`
rc 0; `audit_viewer_decisions.py --no-sweep decks/Spirits/Spirits.cod` rc 0; `check_plan_sig_lockstep.py`
unchanged (only `#BB`). **Smoke** (alone on the box): 191 pass, 4 FAIL -- exactly the 4 Gods keys at integrator
B's numbers (`gods_smoke_d0` 8.7830, `d3` 5.4000, `d5` 5.2000, `gods2hg_smoke_d5` 5.3200; searched slower 1 /
faster 3 / play-changed 79, d0 9/12 -- identical counts): integrator B's Hole A/B movement, GT not yet accepted.
Every other deck byte-identical; play-changed attributable to B2: 0. `check_gt_logs.py` consistent.

### Integrator C1 (2026-11-13): Millicent, Restless Revenant (55c43ab8a)

**Card (Tier 2).** Cost/oracle re-verified against live Scryfall (`logs/spirits/drafts/scry/millicent/`; the
collection JSON had no Millicent row): `{5}{W}{U}`, cmc 7, Legendary Creature -- Spirit Soldier 4/4, keywords
Flying + Affinity, both trigger clauses verbatim ("Millicent or another nontoken Spirit you control dies or deals
combat damage to a player"). Snapshot row added to `scryfall_reference.json`. `vanilla_creature` template.
- **Affinity** on the existing `affinity_for_subtype` (kept: it is the `spirits` routing term). EffectiveSpellCost's
  walk is template-agnostic; unit-pinned on a CREATURE spell: Spirit tokens and a Mimic that named Spirit count,
  opponents' do not, generic only (floor `{W}{U}`).
- **SAME-TURN HOLE, found and fixed (completeness).** The same-turn credit (`SameTurnAffinityDiscountFor`) credits
  only casts ranked STRICTLY before the affinity card. On the generic order Millicent was a tier-10 creature, so she
  TIED every Spirit creature (a tie falls back to plan order, so no credit) and was cast BEFORE Lingering Souls (20).
  A line payable only with the discount was unenumerable at every budget. Two changes:
  1. **NEW generic cast-order tier 25 = AFFINITY** (`GenericProvider::CastOrderRank`, card data only:
     `affinity_for_subtype`), after every creature, Vial put and token sorcery. This is the enabler-before-payoff
     rule the reducer (16) and verse (19) tiers already encode. **[USER, PROVISIONAL] the deck's cast order is
     yours.** This is a generic tier, not a Spirits order: inert for every shipped deck (SliversProvider ranks
     Hivepool 13 itself; only its `MTG_SLIVERS_ORDER=0` A/B arm moves, Hivepool 20 -> 25). Cost: a second Geist of
     Saint Traft cast in the SAME main as Millicent now dies to the legend rule before she is out, so that death
     makes no token. Narrow (both cards in one main) and disclosed.
  2. **`AffinityFeedCount`** (TurnSolver.cpp), shared by the enumeration credit and the executor's line prepay
     (`MTG_PREPAY_LINE_AFFINITY`). It now also counts a VIAL PUT (no def: looked up by name; the prepay already
     counted it, the enumerator did not), a token-making HAND cast (`cast_creates_tokens` with matching token
     subtypes: Souls = 2), and a Mimic by its predicted chosen type (`ChosenCreatureSubtypeId`, the ETB's own
     provider call). It does NOT count a graveyard (flashback) cast: graveyard casts trail every hand cast in both
     worlds. **OPEN hole, disclosed:** "flashback Souls, then Millicent cheaper" in one main.
     `docs/design/graveyard-cast-search-holes-deferred.md` section 3.
  - The other gates already step aside for an affinity cand, so nothing else was needed: ManaPruneBound bails,
    `ManaGateWouldHelp` / selection gate `block` hard, the cost-reframe `interacts`, the ritual-drop bail.
  - Proof: units (Souls + Millicent on 5 lands, Wanderer + Millicent on 4, Vial(2) Shacklegeist + Millicent on 3,
    Mimic + Millicent on 6) are enumerated and paid identically by ApplyPlan and AIEngine::TakeTurn. All four FAIL
    with `MTG_NO_COST_TRICKS=1`. The not-a-free-lunch case: 4 lands admit no Souls + Millicent plan. Scenario
    `spirits_millicent_cast_after_souls_same_main.json` at `--depth 3 --budget-ms 0`: T6. It reads **T7** with the
    credit off.
- **Combat-damage trigger:** NEW `combat_damage_tokens_per_trigger` (flat, ONE token per connecting matching
  creature per damage step, `damaging_pw > 0`) + NEW `combat_damage_watch_nontoken`, inside the shared
  FireCombatDamageTokens. Both worlds reach it through ResolveCombatDamage (GameEngine::CombatPhase,
  TurnSolver's rollout combat). Shroofus is byte-identical: `n = dealt*per_damage + connectors*per_trigger`, and
  the dealt sum is unchanged. The tokens arrive after damage and are summoning-sick. No projection change: draft
  4 is right that they matter only from the next state on, and the leaf / next turn sees them. 2HG: one
  `damaging_idx` entry per attacker, so one trigger per connector (correct). `WolfRunNoCombatDamageWatcher`
  gained the term (conservative). NOT added to DeckUsesSecondMain: post-combat Spirits cheapen nothing, since
  Millicent is already out (draft agrees). Unit: Millicent + Phantom + Shacklegeist + a non-Spirit + two Souls
  tokens connecting = 3 tokens. Geist + its Angel = 1. Scenario `spirits_millicent_combat_tokens.json`: T6 at
  24 life exactly; without the trigger the kill is T7.
- **Dies trigger:** NEW `dies_watch_nontoken`, applied in OnCreatureDies' watcher walk and self block and in
  DeathOfWouldPay (exact: a token victim no longer reads as paying). Unit: a second Geist under the legend rule
  makes 1 token, a Souls token dying makes 0, a nontoken Spirit makes 1, Millicent's own death makes 1 (self
  block), and with her gone, 0.
- **Viewer:** no new decision (affinity is static; both triggers are mandatory and untargeted; a second
  Millicent is the existing legend_keep). INERT_PARAMS += the three new params.
  `audit_viewer_decisions.py --no-sweep decks/Spirits/Spirits.cod` rc 0.
- Routing: the cut-one-card test now also cuts Millicent, and the "none" list drops her too. Without that
  change she alone still routes Spirits, which is correct.

**Verification (2026-11-13, integrator C1).** New `test/unit/test_spirits_millicent.cpp` (13 cases); units
2898/2898 after the routing-test update; scenarios 501/501 (two new); `audit_card_fields.py` rc 0;
`provider_audit.py --check` rc 0; `check_plan_sig_lockstep.py` unchanged (only `#BB`).
**Smoke** (alone on the box): 190 pass, 5 FAIL. Four are the Gods keys at integrator B's numbers (`gods_smoke_d0` 8.7830, `d3` 5.4000, `d5` 5.2000, `gods2hg_smoke_d5` 5.3200; the Hole A/B movement, not yet accepted). The fifth is `extraturns2hg_smoke_d5_s1001`: same score 4.6000, gi19's digest moved. That is NOT this change: re-running the deck alone with the same binary (`--smoke --deck=extraturns2hg`) reproduces the GT digest cc399f1a95984da0 exactly (units 162410 alone vs 220591 in the full run). It is the same budget-timing churn integrator A saw on `extraturns_smoke_d3`. Searched slower 1 / faster 3 / play-changed 80 (B2's 79 + that game); d0 slower 9 / faster 12. Every other deck is byte-identical, Slivers included, so the Vial-put credit moved nothing there. Play changes attributable to C1: 0.

### Integrator C2 (2026-11-13): Skyclave Apparition + White Orchid Phantom

**Cards.** Both re-verified against live Scryfall (`logs/spirits/drafts/scry/c2/`; Skyclave was not in the
collection JSON): Skyclave `{1}{W}{W}` cmc 3 Kor Spirit 2/2, no keywords; White Orchid Phantom `{W}{W}` cmc 2
Spirit Knight 2/2, Flying + First strike, **no flash**. Snapshot rows added to `scryfall_reference.json`. Template
`custom`. Neither param is read by `DetectDecisionProvider` (routing unchanged; `provider_audit.py --check` rc 0).

**Skyclave Apparition (Tier 2).** `ResolveEtbExileUpToOne` (SpellEffects.h) from `FireOwnEtbTriggers`, so a cast
and an Aether Vial put fire it in both worlds. Legal = permanents we do not control, nonland, nontoken, MV <= 4,
no hexproof/shroud: against the goldfish, exactly the spawns (MV 0). The exile is PERMANENT: a NEW marker range
`kSkyclaveExileBase` (-0x20000000, disjoint from Brutal Cathar's `kLinkedExileBase`, so
`ReturnOrphanedLinkedExiles` never returns it) that also encodes the exiled card's OWNER; the source's
`linked_exile_number` is set (already folded into both keys and dominance). LTB (`FireExiledOwnerIllusion`, in
`FireLeavesBattlefieldTriggers`): the owner creates an X/X blue Illusion, X = the exiled card's MV; X = 0 dies at
once (resolved in the shared code, so both worlds agree without relying on an SBA pass); the link is spent, the
card stays exiled; nothing exiled -> no token. The exiled permanent's own leave triggers fire (generic).
- **Target = `DecisionProvider::EtbOptionalExileTargetPick`** (root default, not a Spirits override): the
  largest by effective power, -1 only when nothing is legal. **Why outcome-equivalent (the real game):** the
  spawns never attack or block; no card in the Spirits 60 reads an opponent permanent for value (Shacklegeist's
  tap target only, useless vs a non-blocker); the opponent's lands are not legal; the LTB gift is a 0/0 that
  dies. **What DOES read it:** the learned value leaf's `MidGameFeature::OppCreatures` / `OppTotalPower`
  (KeepModel.cpp ~L440); the board eval otherwise does not. So exile-vs-decline moves a future leaf's feature
  vector, not a game; the default follows the shipped exile-ETB convention (Chupacabra / Cathar) so the Spirits
  artifacts are fitted on it. **[USER, PROVISIONAL]** (the existing ledger item).
- Viewer: `dig` "(may exile)" -- every legal target + "Exile nothing" (main.cpp note, `digPanelHtml` branch,
  DECISIONS.md row, audit manifest `etb_exile_opp_nonland_nontoken_max_mv -> dig`; LTB param INERT_PARAMS).

**White Orchid Phantom (Tier 2).** `ResolveEtbDestroyNonbasicLandOptional` from `FireOwnEtbTriggers`. Legal =
every nonbasic land, both sides. **Own-land targets are NOT dominated, so they are SEARCHED** (the brief asked for
an honest check): trading our nonbasic for a basic always thins the library, and can fix a colour the nonbasic
cannot give a noncreature spell (Unclaimed Territory / Secluded Courtyard pay coloured mana only for Spirit
creature spells; a fetched Plains pays Lingering Souls' {W}), or find the missing colour outright (the scenario:
destroy Godless Shrine, fetch the deck's last Island, cast Supreme Phantom next turn -- T4 instead of T5). It IS
strictly dominated only when no basic land card is left in our library (lose a land, find nothing; nothing in
the 60 reads a land in the graveyard or a land dying), which `SpiritsProvider::OfferOwnNonbasicLandDestroy` prunes
(root hook default: always offer, lossless). Engine:
- **Searched variants (TurnSolver, WHITE ORCHID post-pass beside Timeless Witness's):** in the decision space and
  not in human play, one variant per DISTINCT own nonbasic NAME (battlefield + nonbasics in hand, for this
  turn's land drop) for every `CastFromHand` AND `ActivateVial` of the card, pinned on `Action::tutor_target`.
  Resolution takes a TAPPED copy of that name first (sound: same-name lands differ only in what they can still
  tap this turn); a pin whose land is gone falls back to the fold. `#WO` in both plan signatures (fast + legacy,
  the Vial bucket included, gated on the param). The Vial put now carries the pin in both worlds
  (`apply_vial` / `deploy_via_vial` + the continuation's recorded put), passed to `FireOwnEtbTriggers` only for
  this card. The grouped enumerators' "one Vial put per card" collapse now keeps differently-pinned puts
  (`tutor_target` compared; empty for every other deck, so byte-identical).
- **The basic is searched too:** our destroyed land -> the shared `PerformLandSearch` (basic, 1, tapped,
  optional) consuming a `land_search_pins` ordinal; the in-plan pin fan counts a pinned White Orchid cast/put as
  a cast ordinal (picks + the searched decline). `deck_has_land_search` is stamped for the param.
- **Base (unpinned) = `EtbOptionalNonbasicLandDestroyPick`**: an opponent's nonbasic, else none -- an identity
  fold (the opponent never taps a land; nothing reads its lands, land count or library; the leaf's features are
  ours-only for lands). **The opponent's "may search" is modelled** (the draft's faithful option, cheap): an
  `Opponent Basic Land` placeholder enters TAPPED (numbered after the setup's if Expropriate numbered them).
- An indestructible target is not destroyed but its controller may still search (no "if you do").
- Viewer: `dig` "(may destroy nonbasic land)" -- every nonbasic land, both sides + "Destroy nothing"; our land ->
  `tutor_etb` basic search with decline. No searched variants under human play.

**Real `--claude-play` runs (logs/spirits/integratorC2/cp_driver.py, gi 3):** seed 93 CAST: White Orchid T2
(examined: 3 opponent nonbasics + our Hallowed Fountain + Secluded Courtyard) and Skyclave T3/T4 (examined: the
2/2 spawns); with the human picking our Fountain the `tutor_etb` basic prompt follows. VIAL PUT: seed 43 Skyclave
(vial) T4 and seed 116 White Orchid (vial) T3 both raise the decision.

**Deviations from the drafts.** Template `custom` (both). White Orchid's own-land fold is searched (draft 04) but
the variant also covers nonbasics in hand and is pruned by a PROVIDER hook (not an engine gate); the opponent's
search is the placeholder, not the no-op. Skyclave's hook is a root default rather than a Spirits override (the
draft's design). Stale comments the draft flagged (Fulminator / Acidic Slime "the opponent controls no land")
not touched (not this scope).

### Integrator D (2026-11-13): Usher of the Fallen, the engine's first BOAST (9176533f0)

**Card.** Live Scryfall (`logs/spirits/drafts/scry/usher/usher.json`): `{W}`, cmc 1, Spirit Warrior 2/1, keyword
Boast: "{1}{W}: Create a 1/1 white Human Warrior creature token. (Activate only if this creature attacked this turn
and only once each turn.)" Template `vanilla_creature`; snapshot row in `scryfall_reference.json`; `boast` added to
`audit_card_fields.py` MODELED_ELSEWHERE_KEYWORDS (KeywordFromString throws on it; no combat effect).

**Model.** Slimefoot's `pay_token_cost` (PermAbilityMode::PayToken, {T}-less, paid under the creature-ability
scope, so Secluded Courtyard may pay the {W} and Unclaimed Territory only {C}) + NEW
`activate_only_if_attacked_this_turn` + EXISTING `activate_once_each_turn` (per object, `once_per_turn_used`, set by
ApplyPermAbility's PayToken branch). Both restrictions are checked in the shared `PermAbilitySourceLive`, which the
enumeration (CollectActions), the breakpoint activation keys (CollectActivationKeys), BpAvailablePermAbilityModes,
both apply worlds and SpendRepeatActivations' per-iteration recheck consult; param-gated, so Slimefoot / King
Darien are byte-identical. A once-each-turn PayToken's K axis is {1} per source (two Ushers: K over pass / either /
both, searched; no max-K prune).
- **Attack count:** NEW deck stamp `GameState::deck_has_attack_count` (bitfield in the `deck_has_draw_cap` byte,
  sizeof unchanged), StampDeckTraits mainboard scan. `NoteAttacksDeclared` (both declare sites, GameEngine +
  TurnSolver) now counts, when `deck_has_extra_combat` is off, ONLY attackers carrying the param (no key
  fragmentation); the count and `once_per_turn_used` were already folded into BuildSimKey / dominance / fold keys
  and reset at our untap in both worlds. `deck_has_extra_combat` NOT widened.
- **Post-combat window = SpiritsProvider's restricted second main** (the Speaker / Giada precedent):
  `RestrictedSecondMainLive` = `BoastSecondMainLive` (an own attacked, un-boasted Usher whose {1}{W} the available
  pool can pay -- colour-blind superset, so an over-open phase offers only the pass), `RestrictedSecondMainAdmits`
  = boast activations only, `MayOpenRestrictedSecondMain` true. Already plumbed through `ProviderSecondMainLive`
  at every post-combat site of both worlds (executor TakeTurn gate, rollout m2, FSLineTail, branch sites). The
  rollout m2 is SEARCHED (SolveSecondMainInSearch), and every post-combat host appends the pass, so the boast
  count is searched and holding {1}{W} in main 1 is priced. Human play keeps its FULL main 2 (use_external),
  where the boast is a plan action; it is never offered in main 1.
- **Soundness of the boast-only menu (VERDICT: lossless for this 60, goldfish):** combat creates no resource main 1
  lacked -- no untap, no draw, no mana; the opponent's board is unchanged; Geist's Angel is exiled at end of
  combat; Millicent's combat tokens arrive only while the one-copy affinity card is already out; Wanderer's EOT
  pump and Mimic counters change no cost; nothing keys on the opponent having lost life. So every refused play was
  payable, and weakly better, in main 1. Full argument at `BoastSecondMainLive` (DecisionProviders.cpp).
  Re-derive it for a real opponent or if a combat-resource card joins the list.
- **[USER, PROVISIONAL] main 1 vs main 2 is the user's call per deck.** The narrow boast-only post-combat main
  is the ONLY place the boast is reachable (the restriction makes a main-1 boast illegal), so it is not a
  cast-order choice. No `MTG_*` flag was added (a default-off lever would make the boast unfindable). If the user
  wants Spirits to play a FULL second main instead, that is `uses_second_main`, a profile setting.
- Viewer: label `boast: create a creature token` (PermAbilityLabelFor, only for the param, so Slimefoot's /
  King Darien's label and their references are unchanged); `activate_only_if_attacked_this_turn` in
  `audit_viewer_decisions.py` INERT_PARAMS (a legality gate); `pay_token_cost` already maps to `main_phase`.

**Tests.** `test/unit/test_spirits_usher.cpp` (8 cases, 354 assertions: params; gate attacked/once;
NoteAttacksDeclared counts only the Usher and nothing unstamped; never enumerated in main 1; post-combat K<=1 per
Usher, sick Usher none, two Ushers K in {1,2} + the host's pass; both worlds make two Human Warriors and a hand-built
K=2 block on one Usher fires once; restricted m2 opens/closes, admits only boasts, never narrows a full m2;
Courtyard pays the {W}, Territory x2 does not). Scenarios `spirits_usher_boast_once_after_attack` (T6; T7 without
the window, T5 if it fired twice), `spirits_usher_boast_paid_by_courtyard` (T6), `spirits_usher_boast_d0_runner`
(the d0 runner reaches the window: T6). Unit 2919/2919, scenarios 506/506. Audits: card fields rc 0,
provider_audit --check rc 0, viewer --no-sweep rc 0, plan-sig lockstep only the pre-existing `#BB`.
**Real `--claude-play`** (`logs/spirits/integratorD/cp_usher.py`, seed 101 gi 3, HOLD=3): no boast plan in any
pre_main; T3 post_main offered 10 boast plans (1 Usher payable), T4 133 plans of which 56 with one boast and 3
with two (two attacked Ushers); after a boast the same turn's next post_main offers only the pass.

**Review fixes (D2).** The unit test's pass-offered asserts were wrong (EnumerateMainPlans never emits the pass;
the post-combat hosts append it -- TurnSolver FSLineTail / SolveWithLookahead) and a bitfield bound to `bool&`;
the {1} clamp now keeps an EMPTY count menu empty (a provider prune / nothing affordable).

**Smoke.** Run alone on 9176533f0: 191 passed, 4 failed -- exactly the four gods keys at the C2 checkpoint numbers (d0 8.7830, d3 5.4000 digest-only, d5 5.2000, 2hg d5 5.3200); searched slower 1 / faster 3 / play-changed 79, identical to C2. Every other deck byte-identical (no other deck carries the new params). Nothing accepted.

### Integrator E1 (2026-11-13): the held-fetch crack-then-cast hole FIXED (ea8d143e9)

**Fix (shape 1 of `held-fetch-crack-then-cast-hole.md`).** `AppendHeldFetchCrackFirstPlans` (TurnSolver.cpp) on every
TOP-LEVEL main list (`add_for_land`, the no-drop path of `EnumeratePlansWithLandUncachedBody`, `EnumerateM2PlansBody`):
for each held crack source NAME whose land enters untapped and EVERY `LandSearchPicks` pick (no prune, no cap), the
crack is applied on a copy through the shared `ApplyLandSearchActs` (one-entry `ScriptedLandSearch`) and
`EnumeratePlans` runs on the post-crack board; each plan is emitted with the crack prepended in the PRE-CAST pass
(`land_search_act_slot` 0, or the pre-cast-subset mask when it cracks another source trailing) and the pick in its
`land_search_pins` ordinal. Recursive (two held Strands both first). No new Plan field -- both worlds already run
these at the same point; plan-sig lockstep only `#BB`. Skipped: action-less post-crack plans (identity with the
trailing crack), human play, breakpoint continuations (no pre-cast pass there). `MTG_HELD_FETCH_CRACK_FIRST=0`
restores the hole (A/B only). Shapes 2 (breakpoint) / 3 (optimistic mana source) rejected -- reasons in the doc.
- **It also repairs the rollout's main-2 premise** ("combat creates no new resources, so everything was castable in
  main 1"): a trailing crack's land was new mana nothing in main 1 could spend; Spirits plays no main 2, so before the
  fix even "crack in main 1, cast in main 2" was not played.
- **Residual (OPEN, in the doc):** a breakpoint continuation cannot crack-then-cast what it drew; a non-front
  crack-first pick does not cross with LATER in-plan land-search ordinals (White Orchid's own-land basic) -- the
  additive-axes rule every pin variant follows.
- **Decks that can move:** Spirits (keeps the hold). Burn / Anti-Lifegain / FiveColour decline it, but a fetch they
  play at <= 3 life is held (`ModernFetchCollapsible`), so those states can move. Legacy-shape fetches never hold;
  Wilds / Myriad / Khalni find tapped lands.
- **Viewer: no change needed.** Verified on `--claude-play` (Spirits seed 3, choices `1,2,-1,-1,1,2`): hold the
  Strand T1; T2 commit "land=Plains; Flooded Strand: sacrifice it ...", answer `tutor_etb` with the Island, and the
  SAME pre-combat main re-prompts with Plains + Island untapped and Metallic Mimic / Supreme Phantom offered. The
  play-and-crack line (integrator A's open viewer item) is reachable the same way. The menu is untouched under
  human play, so no reference's menu index moves.

**Tests (written first; budget 0).** `spirits_held_strand_crack_then_cast` (no-drop path, `resume_at`),
`..._with_drop` (land-drop path), `spirits_two_held_strands_crack_then_cast`: T5 FAIL with the lever off -> T4 PASS;
the A repro at `--depth 5 --budget-ms 0` T5 -> T4. Unit `test_held_fetch_crack_first.cpp` (3 cases). Units 2922/2922,
scenarios 509/509; provider_audit --check, audit_card_fields, audit_viewer_decisions --no-sweep rc 0. Burn's "two Lava
Spikes off a held Mesa" passes on both arms (Burn plays a main 2).

**Smoke.** Run alone on ea8d143e9: 191 passed, 4 failed -- the four gods keys with digests identical to D's
(558cea0d / ef690b70 / c3c6b871 / e6f8b68c); searched slower 1 / faster 3 / play-changed 79, unchanged. Every other
deck byte-identical: nothing in the suite moved. (A first smoke attempt ERRORed one scenario because I rewrote
cards.json's Strand note mid-run -- my mistake, not the engine; it passes alone and in the rerun.)
`fetch_hold_review` (USER-owned) can now be measured against a hold that is no longer a turn late.

### Integrator E3 (2026-11-13): human-play payment drop FIXED; every offered plan now realised as offered

**The bug (B's OPEN item).** Repro on the current tree: `test/scenarios/spirits_human_souls_flashback_pays_like_autonomous.json`
(Azorius Chancery + Unclaimed Territory x2, hand Godless Shrine + Lingering Souls, the human commits `land=Godless
Shrine; cast: Lingering Souls, Lingering Souls (flashback)`). B's exact seed-8 prefix no longer reaches that board
(later cards changed the game), so the fixture reproduces B's board directly. **Why the paths differed:** the human
commit and the autonomous executor share the same payer (TapForCostSharedOnce), but `MTG_LINE_SPARE_GENERIC_FIRST`
(2f013bb75: a colour-short line's generic pip taps a SPARE source -- one whose mana no later pip of the line owes,
here a Territory's {C} -- before a coloured one) was gated `!HumanPlayActive()`. Autonomous play paid {2}{W} with
Chancery + Territory and the flashback with Shrine + Territory; the human commit paid a generic with the Shrine and
stranded {B}. With the flag =0 autonomous play drops the flashback too (verified), so this was the WHOLE difference.
**Fix:** the gate is removed (ManaPayment.cpp produce_impl) -- the rule fires only on a colour-short declared line and
only spends a source no later pip of that line can use, so it can never cost the line a cast; the human float hold
still runs whenever it does not fire. Fixture: FAIL (flashback dropped, T7) with `MTG_LINE_SPARE_GENERIC_FIRST=0`,
PASS (no drop, T6) now.

**The general class -- offered plans that cannot be realised.** New `test/human_plan_realised_check.py` (wired into
`test/viewer_checks.sh`): walks Spirits games through `--claude-play`, COMMITS EVERY OFFERED main-phase plan and
asserts the commit's `dropped_casts` equals the plan's own `drops` label. Before: seeds 1-12 = 195 of 748 commits
dropped a cast the menu did not disclose; seeds 13-40 = 6 more. Three causes, all fixed in `TurnSolver.cpp`
EnumeratePlans / EnumeratePlansWithLand (viewer-only, `MTG_HUMAN_DROP_PROBE_ALL`, default ON, =0 restores):
1. **One-ordering plans were never probed** (only multi-hand-cast sets and the mana-suspend case): a hand cast +
   flashback, a Vial put + cast, or a single cast read `not_verified`. Dominant source: the colored_creature_only
   noncreature-pool optimism (`MTG_CCO_NONCREATURE_POOL` off, disclosed in Unclaimed Territory's note) offers
   Lingering Souls / its flashback off a Territory's "any colour". Now every human-menu plan is probed once.
2. **The probe played a GREEDY land** for a plan whose land is not stamped yet (`land_decided` false): "land=none;
   cast: Aether Vial" (seed 22 T2) read `drops` because the greedy Chancery bounced the Vial's only land, yet
   committed clean. The single-ordering probe now plays no land (the enumeration state already holds the plan's).
   The MULTI-ordering probe has the same shape but also dedups orderings by its end state, so changing it could
   change which orderings the menu shows; left as is (no mislabel seen in 40 seeds) -- **OPEN**, surfaced.
3. **Karoo earlier-position clones** (`karoo_before`) inherited the after-the-casts plan's label: "land=Azorius
   Chancery (before Aether Vial)" bounces a Territory whose restricted mana does not float. Each clone is probed.
A dropping plan is LABELLED, never deleted (the g_order_drop_label history: deletion moved Dragonstorm GT and broke a
StompySurprise reference). After: seeds 1-12 = 557 commits / 12 labelled / 0 mismatches; seeds 13-40 = 1489 / 11 / 0.

**Not changed (surfaced):** the Territory any-colour noncreature optimism itself still OFFERS those plans (now
labelled). Turning `MTG_CCO_NONCREATURE_POOL` on would remove them from the menu and from the search's wasted
branches, but moves menu indices and every Territory deck's (Pirates, Soldiers, Vampires, Dragonstorm) search --
a separate, measured change.

**Verification.** Units 2927/2927; scenarios 511/511 (+1); `cast_order_truth_check`, `human_line_order_check`,
`gods_plan_menu_check`, `combo_off_check.sh` (50/50) pass. **Reference replay gate** (`viewer_protocol_check.py
--strict --threads 30`, all 827 refs, every deck -- the payment change reaches every deck's human path): 336 ok, 490
repaired, 1 beaten, **0 play-drift, 0 board-diverged, 0 enum-gap**, rc 0. **Smoke** (alone, on the final binary):
189 passed, 6 failed -- the same six keys with digests byte-identical to E2's (gods 558cea0d / ef690b70 / c3c6b871
/ e6f8b68c; martyr a89c64fb / d4ca095a); searched slower 1 / faster 3 / play-changed 80, unchanged. Autonomous play
is untouched by construction (both changes are HumanPlayActive-only).

**Housekeeping (E2 follow-up).** Stale "every graveyard cast applies after the hand casts" comments refreshed
(BuildGyCopyRuns header, the B1 post-pass, the Retrieval target note); Millicent's bracket note "OPEN ordering hole,
disclosed" -> "ordering hole FIXED 507049f75".

## Approved deferrals

(none yet)

### Smoke checkpoint after C2 (orchestrator, 2026-11-13)
Smoke on 1c7f51311 (C2), run alone: 191 passed, 4 failed — exactly the four gods keys at integrator B's numbers
(d0 8.7830, d3 5.4000 digest-only, d5 5.2000, 2hg d5 5.3200); searched slower 1 / faster 3 / play-changed 79,
identical to B's. Every other deck byte-identical. Nothing accepted (GT accepts are batched at the end).
Live cost audit (`audit_card_costs.py`, 2026-11-13): 599 compared, all match; 7 NON-Spirits cards (Dragonstorm
rituals etc.) NOT COMPARED on HTTP 429 → rc=2 (incomplete, not a pass); every costed Spirits card compared clean.

## Stage 3 — coverage re-check (orchestrator, 2026-11-13, after 7c8bd7fe2)
`analyze_deck.py --coverage-only`: rc 0, `missing: []`, every card `full`, no gaps. Bracket-note classification
of the 14 new cards: PARTIALs are all provably inert vs the passive goldfish and PROVISIONAL pending USER
sign-off (Drogskol hexproof grant; Labyrinth draw cap + abort guard; Wanderer sac-counter; Shacklegeist tap
ability / block restriction; White Orchid flying). The two disclosed OPEN notes are engine holes being FIXED in
this run, not deferrals: Flooded Strand held-fetch crack-then-cast (integrator E1) and Millicent's
flashback-before-hand-cast ordering (E2, `graveyard-cast-search-holes-deferred.md` §3). Every other note reads
FULLY MODELLED. `fetch_hold_review.required = true` (Flooded Strand, hold modelled) — measured after E1.

## Integrators E1 / E2 — search-hole repairs (2026-11-13)
- **E1, held-fetch crack-then-cast:** FIXED ea8d143e9 (ledger f76649649).
  - A Flooded Strand held from an earlier turn now pays for same-turn casts, at every target (`AppendHeldFetchCrackFirstPlans`).
  - Smoke unchanged.
  - Viewer: already expressible, no menu change.
  - Residual: a breakpoint continuation cannot crack a held fetch to cast a card drawn mid-turn. It is inert for Spirits (no mid-turn draws) and recorded in `held-fetch-crack-then-cast-hole.md`.
- **E2, graveyard cast ordering:** FIXED 507049f75 (`graveyard-cast-search-holes-deferred.md` §3). This closes C1's "OPEN ordering hole" for Millicent.
  - Units 2927/2927; scenarios 510/510.
  - Smoke (orchestrator, on a binary rebuilt after an OOM-aborted parallel compile, 19:07): 189 passed, 6 failed.
    - The four gods keys failed, unchanged from B.
    - `martyr_smoke_d0` and `martyr_smoke_d5` changed digest at an UNCHANGED score (6.0680, 5.2500): Lurrus casts now happen at their rank.
    - Searched: slower 1 (the known gods gi19), faster 3, play-changed 80 (+1 vs B). d0 play-changed 920 (+20, martyr).
    - No searched game is slower because of E2.

## Stage 4 — baseline profile + provider (2026-11-13, HEAD after 3cd5102bb)
`analyze_deck.py --no-rebuild` wrote `decks/Spirits/Spirits.profile.json` (card scores + static/default
mulligan; no exhaustive keep/bottom table — that is the later, user-initiated stage). `mulligan_flags: []`.
Cost diagnostic: **COST_NEUTRAL** (the reframe neither helps nor hurts; the generic same-turn credits already
handle the deck's cost interactions). `provider_audit.py --check`: Spirits → **SpiritsProvider**
(cert=NotAssessed), rc 0.
Discard evidence: **DISCARD_UNLABELLED**. 0 real cleanup sheds in 400 d3 games, but 30,685 rollout sheds, all taking
index 0 of the generic ranking. Next step: bound the axis with `MTG_SHED_WORST=1` vs default, ≥150 games per cell, at d3 and d5.

## Stage 5 — verify_deck.py (2026-11-13, first full run)
- **PASS:** coverage (23/23 full), card_costs, card_fields (769 vs snapshot), viewer (self-guard + surface sweep), viewer_wiring
  (bounce, dig, land_entry, tutor_etb, vial_charge), mismatch, play_invariants (8 games / 130 decisions).
  - mismatch: **0 nonconv / 0 fd-diverge** across seeds 7001/8001 × 60 games, both arms.
- **FAIL, as expected at this stage:** regression_tiers, suite (not yet in `test/regression_cases.sh`; added at 4-bis/5j).
- **SKIP:** claude_sweep (running: 16 Sonnet players, seeds 777000+gi, gi 0-15; brief `logs/spirits/sweep/brief.md`).

## Claude-play sweep
- commit: `35b021047` (play-identical to HEAD: later commits are docs and a default-off A/B lever)
- seeds: 777000 games: 16 (gi 0-15, one Sonnet player per game; brief `logs/spirits/sweep/brief.md`)
- flags: 0 unresolved
- Results: 12 ties, 3 player slower (g0, g2, g5: weak signal), 1 player FASTER (g10: player T5, AI T6).
  - **g10:** a MULLIGAN difference, not a search defect.
    - The AI mulligans the player's kept 7 (4 lands, three 3-drops) because of the baseline profile's static `curve_check: "two_drop"`.
    - Handed that hand (`--choices-then-auto`), the search wins T5 at b20 and at b200.
    - A `curve_check: none` copy of the profile also wins T5.
    - This is a USER-owned mulligan decision; the exhaustive keep table (the later stage) supersedes it. Notes: `logs/spirits/g10/notes.md`.
- Raised and dismissed:
  1. Metallic Mimic's (and Territory/Courtyard's) creature-type choice is silent: a documented deck-constant simplification (the card notes).
  2. g7: Lingering Souls listed under `retrace_gy` in the final-state JSON. This is intended: the key lists every graveyard-castable spell (retrace OR flashback), and `flashback_gy` is the subset. Three consumers (index.html, play_invariants.py, viewer_client_check.js) rely on it. Only a stale comment at `src/main.cpp` ~1631 needs rewording.
- Decision types surfaced across the sweep: mulligan, bottom, main_phase, land_entry, bounce, dig (Skyclave Apparition and White Orchid Phantom), tutor_etb (Flooded Strand), vial_charge. Usher's boast was offered only post-combat after attacking.

## Stage 5b/5c/5i + fetch-hold review — one pooled batch (seed 610000, 2,550 games, 2026-11-13)
Manifest: `build/scratch/spirits/s5/manifest.json`. Per-game results via `MTG_DUMP_WINS=1`. Every searched game won.

| cell | avg turn | per-game vs paired cell |
|---|---|---|
| d0 ×500 | 5.1920 | |
| d3 b10 ×300 | 4.6500 | vs d0: 145 faster / 1 slower (gi30, greedy churn) |
| d5 b20 ×300 | 4.6367 | vs d3: 4 faster / 0 slower (monotone) |
| d5 b200 ×150 | 4.6533 | vs b20: 3 faster (gi20, 28, 76) / 0 slower = mild budget starvation, −0.020t |
| 2HG d5 b20 ×100 | 5.2700 | 1 game unwon by T8 |
| `MTG_SHED_WORST` d3 / d5 | 4.6467 / 4.6367 | 1 faster / 0 slower; 0 / 0. **The discard ranking is not load-bearing; no discard ranking can pay.** |
| hold DECLINED d3 / d5 | 4.6500 / 4.6367 | 1 faster (gi20) / 1 slower (gi66), net 0, at both depths. Cost −9.5% units at d3, −11.1% at d5 |

**Fetch hold, the USER's call:** declining the battlefield hold is quality-neutral on 600 paired games and about 10% cheaper.
- The default remains "hold offered" (the root default) until the USER decides.
- `MTG_SPIRITS_NO_FETCH_HOLD` (5e9e03ef3) is the temporary per-job selector.

## RESUME HERE (checkpoint 2026-11-13 ~20:50 UTC, before a context compaction)
**State.** All 14 cards are implemented. Stages 1-4 are done. verify_deck is green except suite/regression_tiers (pending) and claude_sweep (recorded above: 0 unresolved). HEAD has local commits through 5e9e03ef3 plus this ledger commit.
- **Shared branch:** NOT pushed. Backup `origin/wip/spirits-analysis` is at cb5e3181e (stale; re-push the wip branch before risky steps).
- **Shared-branch push owed:** do it in the 4 h window. Run check_references_safe, rebase, rebuild + smoke + check_gt_logs, then a background CI watch.

**Running:** `scripts/leaf_tiebreak_check.py decks/Spirits/Spirits.cod --tag spirits`, output `logs/spirits/tiebreak.log` (Stage 5c2). Read its verdict:
- KEEP / NEUTRAL: nothing to do.
- NO SIGN: re-run at the `--blocks` it suggests.
- OPT OUT: add `GradesNoWinLeaf() false` to SpiritsProvider and record it in `docs/design/horizon-honest-leaf.md`.

**Next steps, in order:**
1. **Suite admission (Stage 4-bis / 5j)**, one Opus agent following `.claude/skills/regression-testing.md` "Adding a deck":
   - `suite_gate.py --cost decks/Spirits` (must be ≤3x Hinata), with the box quiet.
   - Add DECK_FILE/DECK_PROF `[spirits]`, `[spirits2hg]`. Rows on the Merfolk template:
     - smoke: d0 1001×1000; d3 1001×50 b10; d5 1001×100 b20; 2hg d3 ×50.
     - regression: d0 2002×1000; d3 2002×75 b10; d5 2002 and 3003 ×150 b20.
     - overnight ENTRY: d0 4004/6006/8008/10010 ×2000; d3 4004-7007 ×400 b10; d5 4004-7007 ×425 b20.
   - Screen the unbudgeted backstop rows (regression + overnight halves; method in the skill).
   - Run each tier `--deck=spirits`, inspect, `--accept --deck=spirits`, then `python3 test/check_gt_logs.py`.
2. **Orchestrator: full smoke + full regression, then accept.** Expected movement, all explained:
   - gods d0/d3/d5/2hg (integrator B's flashback state-key fix; the one slower game, gods d3 gi19, recovers T6 at d5 and at d8 b0);
   - martyr d0/d5 digests at an unchanged score (E2: Lurrus casts at rank).
   - Overnight gods/martyr keys wait for the periodic overnight run (USER: overnight is occasional).
3. **Discard bucket policy (5i):** the bound is ~0 (the shed ranking is not load-bearing). Still AUTHOR a bucket proposal from cards.json and REPORT it to the USER; implement only on approval (`MTG_SPIRITS_BUCKET_DISCARD`).
4. **5e/5g:** the provider's narrowings to verify against the oracle (MTG_UNPRUNED A/B):
   - the own-nonbasic-land prune when no basic is left in the library;
   - the Skyclave/White Orchid default target pick;
   - the boast-only post-combat main.
   - Optional: `scripts/mine_heuristics.sh` for cast-order candidates.
5. **Housekeeping:** reword the stale `retrace_gy` comment (`src/main.cpp` ~1631, plus `.claude/skills/claude-play.md`).
6. **Re-run `verify_deck.py`** to a full green, then the Stage 6 report.

**Queued follow-up:** `MTG_CCO_NONCREATURE_POOL` (stop enumerating Territory-only Lingering Souls lines; a sound collapse, but it changes Pirates/Soldiers/Vampires/Dragonstorm play and viewer menu indices). Needs its own measured change.

**OPEN USER QUESTIONS** (defaults taken; repeat every one in the final report):
1. PROVISIONAL deferrals:
   - Drogskol hexproof grant;
   - Labyrinth draw cap (abort guard);
   - Wanderer sac-counter;
   - Shacklegeist tap ability, block restriction and flying;
   - White Orchid flying;
   - Skyclave/Orchid default autonomous target;
   - the opponent-basic placeholder for White Orchid's opponent search.
2. SpiritsProvider derives from DeckProvider (not VialProvider).
3. FetchCandidates returns ALL targets (widens the 2026-08-21 one-option rule).
4. The fetch-hold policy: declining is quality-neutral and ~10% cheaper.
5. Cast order: C1's generic rank-25 affinity tier puts Millicent last.
6. Usher: the boast-only post-combat main (main 1 vs main 2 is the USER's per-deck call).
7. The mulligan `curve_check: two_drop` loses g10's 4-land/three-3-drop keep (the keep table supersedes it).
8. Second-main / lord EvalCard under-ranking at d0 (a cast-order hook would be a USER call).

## USER rulings (2026-11-13, after the RESUME checkpoint)
- **Q1 deferrals:** APPROVED except Spirit of the Labyrinth's draw cap. USER: *"I don't know about Spirit of the
  Labyrinth's draw cap being treated like that. Probably best to implement to avoid getting caught off-guard later as
  this can impact a list that has any draw in it. The rest of the deferrals in 1 are okay."* -> implement the cap
  (executor + rollout, every draw site).
- **Q2 (DeckProvider base):** *"2 is fine if you find that more appropriate."* -> kept.
- **Q3 (fetch targets):** PRUNE heuristically, USER order verbatim: *"If we are missing blue or have everything we
  need we get Hallowed Fountain, if we have blue, but are missing black on a non-creature producing land we get
  Godless Shrine. So the priority order would be something like 1 White -> 1 Blue -> 2 White -> 1 Black (that can
  cast lingering souls) -> 3 White -> 2 Blue. The only case I would say we may need to adjust what we search from a
  fixed order is if the line requires a different colour, in which case we can change it to another shockland
  preferably or a basic if not. What we get should reflect what we need. But, because we have shocklands as the
  priority we attempt to fill in the top 2 in priority order otherwise. Once we have all of the shocklands we can
  start taking the basics of course starting with a plains followed by the island and then the last plains."*
- **Q6 (Usher boast-only post-combat main), Q7 (mulligan two_drop):** *"6 and 7 are fine."*
- **Q8 (lords at d0 / rollouts):** *"8 you could try to adjust if it seems to make sense to do so, but I wouldn't
  spend a lot of time. Completely fixing depth 0 and rollouts is a potentially huge task that I'm not too worried
  about for the time being."*
- **Cast order:** the USER asked for a full cast order to review; proposal posted 2026-11-13 (below), NOT adopted
  until the USER approves.
- Still open: Q4 (fetch hold), Q5 superseded by the full cast-order proposal.

### Cast-order proposal (for USER review, not implemented)
land -> Aether Vial -> Metallic Mimic (Spirit) -> Mausoleum Wanderer -> Patrician Geist -> Supreme Phantom,
Drogskol Captain -> Usher, Spirit of the Labyrinth, Shacklegeist, Geist of Saint Traft, Skyclave Apparition ->
Lingering Souls (hand, then flashback) -> White Orchid Phantom -> Millicent. All main 1 except Usher's boast.
Why: Mimic's counter on every later Spirit incl. Souls tokens; Patrician's {1} off a same-turn flashback; Souls
tokens feed Millicent's affinity; White Orchid late so an own nonbasic it destroys has already made its mana.

## Stage 5i — cleanup-discard bucket PROPOSAL (for USER review; NOT implemented)
Measured bound first (seed 610000 batch): shedding the WORST card instead of the generic ranking moved 1 game of 300
at d3 and 0 at d5, so the discard ranking is barely load-bearing for this deck. Proposal anyway (discard FIRST ->
keep LAST), from cards.json:
1. **Lingering Souls** while we control an unrestricted black source (Godless Shrine / Watery Grave) or a Flooded
   Strand that can still fetch one: in the graveyard it is still two 1/1 fliers for {1}{B}.
2. **Surplus lands** (a land in hand beyond the next drop, once we control 5+ lands): Azorius Chancery first, then
   basics, then shocks, Flooded Strand last.
3. **A surplus Aether Vial** (one already on the battlefield).
4. Goldfish-inert or small bodies: Skyclave Apparition (2/2 for 3, ETB inert), Shacklegeist (2/2), Mausoleum
   Wanderer (1/1), Usher of the Fallen (2/1), Metallic Mimic (2/1), Spirit of the Labyrinth (3/1),
   White Orchid Phantom (2/2 flying first strike).
5. Geist of Saint Traft (2/2 + a 4/4 attacking Angel).
6. Lords: Patrician Geist, Supreme Phantom, Drogskol Captain.
7. Millicent, Restless Revenant (cheaper with every Spirit; keep).
### Integrator F (2026-11-13): Spirit of the Labyrinth's draw cap, MODELLED (7421628b0)
USER (Q1): *"Probably best to implement to avoid getting caught off-guard later as this can impact a list that has
any draw in it."* The abort guard is gone; `handentry::NoteFirstDraw` is miracle-only again (its pre-guard form).
- **Rule (core/DrawCap.h):** with any permanent carrying `each_player_draw_cap_per_turn` on the battlefield, a
  player's Draw/DrawStep past the cap this turn is IGNORED (counts draws made before the Spirit entered; no
  deck-out; no replacement -- tested before dredge, and `CycleDrawReplacer` offers no Cyclone walk; non-draw hand
  entries untouched; a loot's discard still happens). Deck-gated on `deck_has_draw_cap`.
- **Choke point:** `AttemptDrawIntoHand` (every draw in both worlds: draw step, DrawIntoHand[Detailed], TrickDraw,
  PerformDrawX, dig/cycle loops, upkeep draws). Counter fixes so the count is current before each draw: modal
  draws (EffectHandler + TurnSolver twin) and Braingeyser now count per draw; Howling Mine / Horn of Greed test
  the cap before their empty-library loss.
- **Turns:** `drawcap::TurnBoundary` at our untap and at the start of the opponent's notional turn, in both worlds;
  the opponent's notional draw is counted under a cap (so their Howling Mine draw is capped); Asylum Visitor's
  count restore is skipped under a cap. Opponent `ForcedDraw` clamps; `ForcedDrawLethalX` reads the clamp.
- **Search side:** `BuildSimKeyBase` folds `cards_drawn_this_turn` for a cap deck (Dominance already folded it);
  plan signatures need nothing (no new action). Predictors: the Fists kill projection uses `drawcap::Allowed`.
  Draw-step-based predictors (keep model, mulligan/ideal-order tiers, Combat's next-turn draw projection) are
  unaffected: the draw step is a turn's first draw. Over-inclusive "this card draws" classifiers (breakpoint /
  hand-entry gates) were left as is -- they can only add work, never hide a line.
- **Verification:** unit 2935/2935 (new `test_spirit_labyrinth.cpp`, 9 cases, both worlds); scenarios 513/513
  (new `spirits_labyrinth_caps_howling_mine` T5 + control twin T4); `check_plan_sig_lockstep` only the known `#BB`;
  `audit_viewer_decisions.py --no-sweep` rc 0 (INERT_PARAMS text updated). Play-neutrality: Spirits 50 games
  seed 610000 d3 b10, per-game logs byte-identical vs `build/PGO/mtg` (stamped HEAD:src). Smoke not run (orchestrator).


## Measurements after the USER rulings (2026-11-13 evening)
- **Horizon tie-break (5c2):** `leaf_tiebreak_check.py` 0 changed games of 12,000 paired -> the lever never binds on
  Spirits; default kept (the tool's "re-run at --blocks 48" is moot at 0/12,000; re-run only if asked).
- **Fetch rule (3b41ab662) vs the every-target fan**, seed 620000, pooled, both arms in one batch:
  d0 600: -0.0017 (1 faster / 0 slower); d3 400: +0.0075 (1/4); d5 400: +0.0075 (1/4); units -21% (d3) / -25% (d5).
  With MTG_NO_SEARCH_SHUFFLE=1 (both arms): d3/d5 +0.0050 (0/2), units -18% / -26%. The two remaining slower games
  (gi120, gi399) are DRAW THINNING: the unpruned search cracks the Strand for the exact land that is the next draw
  (gi399: the Plains drawn T3; gi120: the Island drawn T3), which only a library-order-aware search can do; with the
  shuffle on, the extra slower games are the shuffle re-roll (gi120: the post-fetch draws differ from T3). The
  rule's measured cost is therefore clairvoyance; KEPT ON (the USER's rule).
- **5e narrowings vs MTG_UNPRUNED=1**, seed 630000 d3/d5 400 each: d3 -0.0025 (5 faster / 4 slower), d5 -0.0050 (4/2),
  units +17% / +29%. Every one of the 9 differing games reproduces with ONLY `MTG_UNPRUNE=fetch,landpick` opened ->
  the own-nonbasic prune, the Skyclave/Orchid default target and the boast-only main 2 changed no game; the fetch
  gate is the fetch rule above (clairvoyant thinning both ways).
- **Q8 lord value (MTG_SPIRITS_LORD_VALUE)**, seed 630000 paired: d0 1000: 5.1900 -> 5.1220 (90 faster / 23 slower,
  t -6.4); d3 400: -0.0075 (3/0); d5 400: -0.0025 (1/0); units lower at d3/d5. Clean win -> ADOPTED default ON
  (4b9c49ac5) per the USER's adopt-clean-wins rule.

## RESUME HERE (checkpoint 2026-11-13 ~22:20 UTC; supersedes the ~20:50 block above)
**Done since:** USER rulings recorded; fetch rule 3b41ab662 (+ FetchLandSearchPickWidth hook); lord value adopted
4b9c49ac5; Labyrinth draw cap MODELLED (integrator F, 352969c1b); retrace_gy housekeeping; measurements above;
units 2943/2943, scenarios 513/513; smoke = only the 6 explained gods/martyr keys -> ACCEPTED 67eb90b2a.
**Running:** full regression tier (`logs/spirits/regr_int.log`), to accept the same explained gods/martyr movement.
**Next:** (1) inspect + accept regression (only gods/martyr keys may move); (2) suite admission via one agent from
`logs/spirits/suite-admission-brief.md` (box quiet for the cost gate); (3) verify_deck to green; (4) shared-branch
push in the 4 h window (check_references_safe, rebase, rebuild, smoke, check_gt_logs, CI watch); (5) Stage 6 report.
**Open USER questions:** cast-order proposal; discard-bucket proposal; Q4 fetch hold; the creature-only-land reading
(W/U yes, B no) and the basic order (Plains while lib Plains > lib Islands, else Island) in the fetch rule; Labyrinth
ruling (a capped draw cannot be replaced by dredge/Cyclone).

### Regression tier on a0acbea6b (orchestrator, 2026-11-13 ~22:45 UTC)
301 passed, 8 failed. Accepted (`--deck=gods,gods2hg,martyr`): the 7 gods/martyr keys, same causes as smoke (B's
flashback state-key fix, E2's graveyard cast at rank). The one searched-slower game, gods d5 s2002 gi14 T6->T7,
recovers T6 at d8 b0 (budget churn). NOT accepted: `extraturns_regression_d5_s2002` (same score 4.6467, digest
moved in ONE game, gi66). Re-played in its own batch, gi66 gives the GT per-game hash 9eb685ab on BOTH the
pre-change binary (PGO, src b3928644 = 861cfac7b) and HEAD; the tier run's f79a24d2 is pooled-run churn, not a
code change from this work (kept GT; flag if it recurs -- cf. the thread-dependent batch digest note).

## Suite admission (integrator G, 2026-11-13, on b53b321f1, build/Release)
- **Cost gate** (`suite_gate.py --cost`, one pooled regression run of spirits + hinata + hinata2hg, 32 threads):
  spirits worst searched shape = d5 b20, **1380.8 ms/game** (d3 b10: 806 ms/game); Hinata (pinned yardstick) worst =
  hinata2hg d5 b30, **4449.9 ms/game** -> **0.31x** (limit 3x) -> PASS. Spirits total 502.0 core-s over 6 jobs.
- **Rows** (Merfolk template; no value_play block, so play = d5 b20): smoke d0 1001x1000, d3 1001x50 b10, d5 1001x100
  b20, spirits2hg d3 1001x50 b10; regression d0 2002x1000, d3 2002x75 b10, d5 2002/3003 x150 b20; overnight ENTRY
  d0 4004/6006/8008/10010 x2000, d3 4004-7007 x400 b10, d5 4004-7007 x425 b20. `DECK_FILE`/`DECK_PROF` `[spirits]` only
  (the `2hg` suffix is stripped by regression.sh; no alias entry, per the skill and the merfolk2hg precedent).
- **Unbudgeted backstop**: screened 12 twenty-game blocks (30001..30221 step 20, d3+d5, budget 0, abandon_units 2e6,
  MTG_SLOW_GAME_MS=5000): 10 of 469 games hit the cap (blocks 30001/30021/30041/30101/30161/30221). Regression half
  `spirits 3|5 30181 x20` (no game over 5 s at either depth); overnight half `spirits 3|5 30081 x10` (seeds
  30081..30090, all under 5 s; the block's one ~13 s game, gi14 at d3, is outside the row). Note the deck's unbudgeted
  tail (~2% abandoned) is similar to the "1 in 20" decks.
- **Accepted GT** (all NEW keys, every game played, no abandonment; `--accept --deck=spirits[,spirits2hg]`; the GT
  diff showed only these keys + the regression header):
  smoke d0 5.1240, d3 4.5600, d5 4.6500, 2hg d3 5.1600;
  regression d0 5.0770, d3 4.6533, d5 s2002 4.6733, d5 s3003 4.6933, backstop d3/d5 s30181 4.5500;
  overnight d0 5.1225/5.1330/5.1275/5.1135, d3 4.6500/4.6475/4.6225/4.6850, d5 4.6400/4.6565/4.6306/4.6659,
  backstop d3/d5 s30081 4.5000. The regression run also re-measured hinata/hinata2hg: all 7 keys PASS; the reference
  gate (VPC --strict) 0 play-drift / 0 board-diverged / 0 enum-gap; suboptimal refs all beaten.
  The overnight rows ran `MTG_PGO=0` (Release; PGO stamp was stale and builds are byte-identical by contract).
- `check_gt_logs.py`: 1147 consistent, 0 stale, 0 missing. `suite_gate.py --require decks/Spirits`: ok.
- `verify_deck.py decks/Spirits/Spirits.cod`: **GATE PASS** -- coverage, card_costs, card_fields, regression_tiers
  ("in all 3 suite tiers with GT"), viewer, viewer_wiring, mismatch, play_invariants, claude_sweep, suite all PASS;
  clause_ledger SKIP (standing). Note: it says the claude_sweep was recorded at 35b021047 (re-run only if play changed).

## RESUME HERE (checkpoint 2026-11-14 ~00:00 UTC; supersedes earlier blocks)
**Done:** suite admission (integrator G: cost 0.31x Hinata; rows in all 3 tiers); REBASED onto origin (181 commits;
backup branch `backup/spirits-pre-rebase`), rebuilt, units 3055/3056 (the 1 failure = test_pirates_provider's
deck-folder count: 12 UNTRACKED leftover folders in decks/ on this box, not code), scenarios 530/530, full smoke =
only the explained gods/martyr keys; re-accepted gods/martyr/spirits smoke+regression and spirits overnight
(Spirits byte-identical to pre-rebase in all tiers) -> a819bc2d6; check_gt_logs 1170/0/0; **shared branch PUSHED
a819bc2d6 at 2026-11-13 23:53 UTC** (next shared push not before ~03:55 UTC). CI run 43530446190 was in progress --
check `gh run view 43530446190` (report Windows result).
**In progress:** final verify_deck. The first try was run with the FOLDER (`decks/Spirits`) instead of the .cod ->
viewer (IsADirectoryError) and regression_tiers FAILs are that mistake; re-run as
`python3 scripts/verify_deck.py decks/Spirits/Spirits.cod` (log logs/spirits/verify_final.log). It also reported
`play_invariants 8 invariant violation(s)` -- INVESTIGATE (possibly also the wrong path; else new since the rebase).
**Then:** Stage 6 report with 6a disclosure.
**Open USER questions:** cast-order proposal; discard-bucket proposal; Q4 fetch hold; fetch-rule readings
(creature-only lands count W/U not B; basics: Plains while lib Plains > lib Islands, else Island); Labyrinth ruling
(a capped draw cannot be replaced by dredge/Cyclone); extraturns_regression_d5_s2002 one-game pooled churn (kept GT).

## USER rulings 2026-11-14 + cast order / discard buckets / fetch hold LANDED (uncommitted at write time)
- **Cast order:** *"The cast order looks good, though we should make a full order with 6 and 7 split. Most cards don't
  care about the order except Metallic Mimic, Geist/Lingering Souls and Millicent, so it should work fine."* ->
  `SpiritsProvider::CastOrderRank` (`MTG_SPIRITS_CAST_ORDER`, default ON), one spot per card: Vial 101 -> Mimic ->
  Wanderer -> Patrician -> Supreme Phantom -> Drogskol Captain -> Usher -> Labyrinth -> Shacklegeist -> Geist of
  Saint Traft -> Skyclave -> Lingering Souls -> White Orchid -> Millicent 114 (splits AGENT, cheapest first).
- **White Orchid Phantom on our own land:** USER *"I'm not sure we want to destroy our own land, though you are right
  that it does refresh it, effectively producing mana... Interesting."* No change: the own-nonbasic target is a
  SEARCHED option ("up to one"), offered only while a basic remains (OfferOwnNonbasicLandDestroy).
- **Discard:** *"For the Discard order we should have actual buckets. i.e. We should hold at least 3-4 threats of
  varying costs and at most 3 lands and an Aether Vial for deployment. Most of the time we are probably okay with 3
  lands total, though 4 or 5 are okay to hit if we can keep 4 threats in hand. I agree that Lingering Souls should be
  dropped early on the threat side."* -> `SpiritsShed` (`MTG_SPIRITS_BUCKET_DISCARD`, default ON; full rule and the
  AGENT readings in the code comment): surplus lands (past 5 in total; past 3 when 4 threats cannot be kept) -> surplus
  Vial -> Lingering Souls with black access (still counts as a threat from the graveyard) -> threats past the kept 4
  (varying costs first) -> 4th/5th lands -> 4th threat -> the Vial -> core 3 threats -> kept lands.
- **Q4 fetch hold:** *"Yes, we should drop the fetch hold. It isn't helpful here."* -> `MTG_SPIRITS_NO_FETCH_HOLD`
  default ON (=0 restores the hold).
- **Fetch-rule readings (creature-only lands W/U not B; basics Plains while lib Plains > Islands):** *"2-3 on Fetches
  sounds good."* APPROVED.
- **Labyrinth vs dredge:** *"4 on Spirit of the Labyrinth I agree that your interpretation should be correct. The draws
  don't happen at all, so there is nothing to replace."* Plus the nuance: *"If you draw a card, for subsequent draws you
  cannot dredge, so that part is correct. However, if you dredge all previous cards subsequent draws are not blocked yet
  because dredging does not count as a draw. That means you can continue dredging."* / *"So, you have to actually draw
  at some point for the block to kick in."* The choke point already did this (a dredge returns drew=false; the cap
  reads actual draws). FIXED the 10 single-draw sites that counted `hand.size()` growth as draws (a dredge would have
  consumed the cap): they now add `DrawIntoHand`'s actual-draw count (SpellEffects.h x8, EffectHandler modal draws,
  TurnSolver twin). Pinned by test_spirits_discard.cpp "dredge, dredge, draw, then the cap".
- **Measurement** (held-out seed 640000, paired; arms new / nocast / nobucket / old = all three off):
  | lever | d0 x1000 | d3 b10 x400 | d5 b20 x400 | units |
  |---|---|---|---|---|
  | fetch hold dropped | -0.131 (132 faster / 2 slower) | -0.0025 (1/0) | 0 | d3 -9%, d5 -23% |
  | USER cast order | -0.004 (4/0) | same outcomes | same outcomes | -0.5% |
  | discard buckets | digest-identical | identical | identical | ~0 |
  Buckets: index 0 differs from the generic ranking in 87% of rollout sheds, but only 28 real sheds in 9,600 games
  and none moved a game (the 2026-11-13 bound said ~0). Old -> new: d0 5.1040 -> 4.9690, d3 4.6125 -> 4.6100, d5
  4.6075 -> 4.6075.
- Units 3060/3061 (the deck-folder count, environmental); scenarios 530/530.

## RESUME HERE (checkpoint 2026-11-14 ~00:50 UTC; supersedes earlier blocks)
**Done since the last block:** USER rulings of 2026-11-14 (section above) all IMPLEMENTED and measured (clean win,
all default ON): full cast order, discard buckets, fetch hold dropped, dredge-vs-cap draw-count fix. Units 3060/3061
(deck-folder count, environmental), scenarios 530/530. CI for the a819bc2d6 push: ubuntu, windows, determinism
parity ALL SUCCESS.
**Running (harness-tracked, this session):** `bash test/regression.sh --smoke` then `--regression` (Release, dirty
src at launch = this commit's src) -> logs/spirits/smoke_castdisc.log, logs/spirits/reg_castdisc.log (each ends
`rc=N`). Do NOT rebuild until both finish.
**Next, in order:**
1. Read both tiers' audit (audit_changed_games). Expected movement: spirits keys (cast order / hold / buckets).
   Any other deck moving = the draw-count fix (10 single-draw sites now add DrawIntoHand's actual draws, not hand
   growth) -- should be identical unless a dredge happens at one of those sites; root-cause anything else.
2. Accept smoke + regression (`--accept`, scoped or full as the audit warrants; ACCEPT note ONE line), then
   `python3 test/check_gt_logs.py`. Spirits overnight keys wait for the periodic overnight run.
3. `python3 scripts/verify_deck.py decks/Spirits/Spirits.cod > logs/spirits/verify_final.log` (the .cod, NOT the
   folder); INVESTIGATE the earlier `play_invariants 8 invariant violation(s)` if it recurs.
4. Shared-branch push not before ~03:55 UTC 2026-11-14 (4 h rule); then background CI watch (--interval 60).
5. Stage 6 report with 6a disclosure.
**Open USER questions:** extraturns_regression_d5_s2002 one-game pooled churn (GT kept); whether to re-run the
claude-play sweep (recorded at 35b021047, before the fetch/cast-order/lord changes; 0 flags; agent recommends no).
