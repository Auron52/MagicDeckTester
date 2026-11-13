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
| Millicent, Restless Revenant | 2 | `affinity_for_subtype` Spirit; per-connecting-nontoken-Spirit combat-damage token; `dies_watch_nontoken` | none | drafted |
| Usher of the Fallen | 3 | first BOAST: `pay_token_cost {1}{W}` + new `activate_only_if_attacked_this_turn` + `activate_once_each_turn`; a post-combat main opened only when a boast is live | A (main-2 plan action) | drafted |
| Lingering Souls | 2 | `cast_creates_tokens 2` Spirit fliers + `flashback_cost {1}{B}`; FIXED Hole A (flashback in DeckGraveyardReaders / BuildSimKey / Dominance; moves Gods) + Hole B (per-copy + PENDING flashback candidates, `SubsetHasNonCanonicalGyCopies`); shared `GraveyardCastSpellCost`; executor cast-count drift fixed | A (`flashback_gy`, `main_phase` plans incl. hand cast + flashback) | IMPLEMENTED 8097213a2 |
| Spirit of the Labyrinth | 1 | 3/1 body; draw cap inert in this 60 (no card draws) + fail-closed abort guard `each_player_draw_cap_per_turn` (deck constant `deck_has_draw_cap`, handentry::NoteFirstDraw) | none | IMPLEMENTED 87078b215 |
| Skyclave Apparition | 2/3 | new optional ETB exile opp nonland nontoken MV≤4 (opponent spawns are legal targets in 8/10 games) + LTB Illusion | A (`dig` "(may exile)") | drafted |
| Shacklegeist | 1 | vanilla 2/2 flier (NO flash per live oracle); tap ability / block restriction PROVISIONAL inert | none | IMPLEMENTED 87078b215 |
| White Orchid Phantom | 2 | `etb_destroy_nonbasic_land_optional` (opponent placeholder lands 0-3 nonbasic since 2026-11-05) | A (`dig` may-destroy, `tutor_etb`) | drafted |
| Flooded Strand | 1 | Arid Mesa battlefield-fetch schema, Plains/Island; routed to SpiritsProvider (lossless fetch fan, hold kept) | A (fetch, hold, shock) | IMPLEMENTED 87078b215 |
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
- **[USER, PROVISIONAL default taken]** Spirit of the Labyrinth's draw cap deferred as inert (no draw in the 60), with an abort guard if any second draw ever happens under it.
- **[USER, PROVISIONAL]** Skyclave Apparition's exile target folded to one default (opponent's largest creature, Brutal Cathar convention) in autonomous play — all choices outcome-equivalent vs the goldfish; the human sees all.
- **[USER, PROVISIONAL]** Shacklegeist's tap ability + blocking restriction, flying (no blocking anywhere) deferred as inert.
- **[USER, PROVISIONAL]** White Orchid Phantom: opponent's "may search for a basic" a no-op (Path to Exile precedent).
- **[USER]** Fetch hold policy for Flooded Strand — measure first with full timing (provider returns every distinct target, hold offered), then A/B named policies.
- **[USER, PROVISIONAL]** Drogskol Captain's hexproof grant deferred as inert (the opponent never targets; Lord of Atlantis islandwalk precedent, approved 2026-11-01).
- **[USER, PROVISIONAL]** Mausoleum Wanderer's "sacrifice: counter target instant/sorcery unless its controller pays {X}" deferred. Only target is our own Lingering Souls; it can beat keeping Wanderer only with Millicent on the battlefield (sac → nontoken Spirit dies → 1/1 token), a narrow window disclosed here. Tier-3 alternative: a Remand-style pay-branch rider.
- **[USER]** Fast playouts / d0 may under-rank lords (`EvalCard` scores a lord by its own body). The search's combat applies the bonus correctly; a cast-order hook would be a user call.
- Flash is inert engine-wide (no off-main-phase casting window); weakly dominated vs a passive goldfish.

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
- **OPEN engine hole (not fixed):** a Strand already HELD on the battlefield cannot be cracked to pay for a
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

## Approved deferrals

(none yet)
