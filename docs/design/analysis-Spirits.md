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
| Lingering Souls | 2 | `cast_creates_tokens 2` Spirit fliers + `flashback_cost {1}{B}`; FIX Hole A (flashback missing from state key / dominance, 3 sites; touches Gods) + Hole B (hand-cast→flashback and 2× flashback in one main unfindable) | A (`flashback_gy`) | drafted |
| Spirit of the Labyrinth | 1 | 3/1 body; draw cap inert in this 60 (no card draws) + fail-closed abort guard `each_player_draw_cap_per_turn` (deck constant `deck_has_draw_cap`, handentry::NoteFirstDraw) | none | IMPLEMENTED 87078b215 |
| Skyclave Apparition | 2/3 | new optional ETB exile opp nonland nontoken MV≤4 (opponent spawns are legal targets in 8/10 games) + LTB Illusion | A (`dig` "(may exile)") | drafted |
| Shacklegeist | 1 | vanilla 2/2 flier (NO flash per live oracle); tap ability / block restriction PROVISIONAL inert | none | IMPLEMENTED 87078b215 |
| White Orchid Phantom | 2 | `etb_destroy_nonbasic_land_optional` (opponent placeholder lands 0-3 nonbasic since 2026-11-05) | A (`dig` may-destroy, `tutor_etb`) | drafted |
| Flooded Strand | 1 | Arid Mesa battlefield-fetch schema, Plains/Island; routed to SpiritsProvider (lossless fetch fan, hold kept) | A (fetch, hold, shock) | IMPLEMENTED 87078b215 |
| Hallowed Fountain | 1 | Steam Vents schema | A (`land_entry`) | IMPLEMENTED 87078b215 |
| Drogskol Captain | 1 | `lord_effect` Spirit +1/+1, `lord_excludes_self`; hexproof grant PROVISIONAL inert | none | IMPLEMENTED 87078b215 |
| Supreme Phantom | 1 | `lord_effect` Spirit +1/+1, excludes self | none | IMPLEMENTED 87078b215 |
| Patrician Geist | 2 | Spirit lord + new `reduces_graveyard_cast_generic 1` (Souls flashback {1}{B}→{B}) at every gy-cast price/pay site incl. same-turn ManaPruneBound credit; NOT `reduces_spell_subtype` (→ goblin route) | none | drafted |
| Mausoleum Wanderer | 2 | +1/+1 UNTIL EOT (not a counter) when another Spirit enters: `own_creature_enters_self_temp_power/_tough` + `enters_watch_subtypes Spirit`, add to `DefHasCreatureEnterWatcher` | none | drafted |

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

## Approved deferrals

(none yet)
