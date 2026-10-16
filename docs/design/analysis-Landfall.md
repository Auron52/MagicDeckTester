# Analysis ledger — Landfall

Deck: `decks/Landfall/Landfall.cod` (mono-green ramp/landfall with a red splash: Avenger of Zendikar,
Primeval Titan, Rampaging Baloths, Omnath Locus of Rage, Phylath World Sculptor, Khalni Heart
Expedition, Courser of Kruphix, Genesis Wave, Terastodon; ramp via Rampant Growth, Search for Tomorrow,
Cultivate, Evolving Wilds / Terramorphic Expanse / Myriad Landscape). The user dropped the list into the
primary checkout on 2026-10-15 and asked for an unattended overnight analysis.

Branch/worktree: `landfall-analysis` @ `/workspaces/MagicDeckTester2/.claude/worktrees/landfall`
(cut from origin `8bdc9646`). The decklist was COPIED here; the user's original stays in the primary
checkout untouched.

## Standing decisions (pass to every subagent)
- Goldfish: the opponent never casts spells or blocks; opponent-facing triggers are inert, but
  self-binding clauses bind us. Model the full card; goldfish-inert deferrals are PROVISIONAL until
  the user signs off.
- No greedy / heuristic substitute inside the search window (user hard rule). Choices a card
  introduces (which lands to fetch, Cultivate's split, Phylath's Plant target, Courser hand-vs-top
  land, activation timing) are SEARCHED, provider-narrowed (restrict/order only), or collapsed by a
  stated sound dominance fold — and surfaced to the viewer.
- Cards are generic (cards.json is deck-agnostic).
- Cast order / range / main-split / mulligan / discard buckets are USER-OWNED: author and present,
  never adopt unilaterally.
- One heavy job (build or batch) at a time; total threads <= 24.
- Agents: no `cd` in compound commands, no `rm -rf`, no `timeout`.

## Stage 1 — coverage (2026-10-15)
Missing (15): Rampaging Baloths, Avenger of Zendikar, Khalni Heart Expedition, Search for Tomorrow,
Cultivate, Primeval Titan, Evolving Wilds, Myriad Landscape, Radiant Fountain, Phylath World Sculptor,
Courser of Kruphix, Omnath Locus of Rage, Haunted Fengraf, Terramorphic Expanse, Rampant Growth.
Present (5): Forest, Mountain, Genesis Wave, Gruul Turf, Terastodon. No sideboard.

**Central engine gap:** the engine has NO triggered landfall. The only "landfall" was Searing Blaze's
"if a land entered this turn", keyed on `lands_played_this_turn`; a land PUT onto the battlefield
(EnterLand) counted for nothing (Genesis Wave's note said so). Six cards here are landfall payoffs
and most of the rest put lands onto the battlefield, so a generic landfall dispatch at the shared
land-entry chokepoint is the foundation of the analysis.

## Stage 2 — cards (in progress)

Work split (2026-10-15 ~13:50 UTC):
- Agent A (implementing): landfall dispatch + Baloths, Avenger, Omnath (incl. Elemental-dies trigger
  via legend rule), Phylath, Courser lifegain, Khalni quest counters.
- Agent B (research): Rampant Growth, Search for Tomorrow (+suspend), Cultivate, Evolving Wilds,
  Terramorphic Expanse, Myriad Landscape, Khalni activation, Primeval Titan (ETB + attack).
- Agent C (research): Courser of Kruphix play-lands-from-top.
- Agent D (research): Radiant Fountain, Haunted Fengraf; review of the five present cards.

Drafts: `logs/landfall_drafts/` (gitignored).

### Card table
| Card | Tier | Summary | Deferrals (status) |
|---|---|---|---|
| Rampaging Baloths | 3 | `landfall_creates_tokens` 4/4 Beast (current Oracle has no "may"). Trample inert in goldfish. | — |
| Avenger of Zendikar | 3 | `etb_tokens_per_land` 0/1 Plants; `landfall_counters_each` on every Plant. Avenger is itself an Elemental. | "you may" always taken (all-or-nothing; a counter never hurts without undying) — PROVISIONAL |
| Omnath, Locus of Rage | 3 | landfall 5/5 R/G Elemental (multi-colour tokens added); Elemental-dies 3 damage via `dies_watch_subtype` (Pashalik Mons shape); legend-rule deaths count. | "any target" always the opponent's face, human not asked (Pashalik precedent) — PROVISIONAL |
| Phylath, World Sculptor | 3 | Plants per BASIC land (`IsBasicLandCard`); landfall four counters on TARGET Plant — searched rank over identity-folded, provider-ordered classes; human picks on board (`target`); CR 603.3d under a simultaneous Wave. | — |
| Courser of Kruphix | 3 | landfall lifegain (078acb9e); play lands from the library top (`Plan::land_from_top`, branched vs hand lands, site-10 re-solve on a new top, greedy-site awareness, viewer `library_top` + `landtop=`); reveal display-only. | Titan-attack-shuffle main-2 drop not built (KNOWN GAP, user Q4) |
| Khalni Heart Expedition | 3 | landfall quest counter (078acb9e); activation: see the row below (landfall-search). | "you may" counter always taken (only feeds an optional activation) — PROVISIONAL |
| Rampant Growth | 3 | first user of the shared `src/core/LandSearch.h` helper: one basic, tapped, via EnterLand (landfall fires); one shuffle even on fail-to-find. | fail-to-find / "up to" collapse — PROVISIONAL |
| Search for Tomorrow | 3 | basic UNTAPPED; Suspend 2—{G} as a plan action (`ApplySuspend` pays {G}); off suspend cast at the next upkeep before the draw, to graveyard, counts for storm. | "you may cast it" off suspend collapsed to cast (human too) — PROVISIONAL. Gap: a hard-cast's untapped land is not counted for later casts in the same plan. Suspend was never chosen in 24 sample games — to investigate. |
| Cultivate | 2 | ordered (battlefield, hand) basic pair; the hand basic can be the same turn's land drop via the put-in-hand breakpoint. | "up to two" collapsed to two — PROVISIONAL |
| Evolving Wilds / Terramorphic Expanse | 3 / 1 | real lands with no mana ability (`template: custom`); searched `SacLandSearch` activation; folded as one card for Titan picks. | instant-speed window collapsed to main phase — PROVISIONAL |
| Myriad Landscape | 2 | {2},{T}, sac: two basics sharing a land type; its {T} is paid first so it can't pay its own {2}. | "up to two" collapsed; instant window collapsed — PROVISIONAL |
| Khalni Heart Expedition (activation) | 3 | `QuestSacLandSearch` (no {T}); a crack made possible mid-plan is reachable the same main (new breakpoint fan-out clause); IN-COMBAT crack after Titan's attack trigger (follow-up a). | post-combat windows collapsed (in-combat crack weakly dominates) — PROVISIONAL |
| Primeval Titan | 3 | ETB + attack trigger, the attack resolved before damage in both worlds; attacker indices repaired after a Turf bounce; the attack projection prices the Plant counters. | Titan's "you may search" collapsed to the fullest pick — PROVISIONAL |
| Radiant Fountain | 1 | `produces C`, `etb_lifegain 2` (fires when put too). | — |
| Haunted Fengraf | 2 | one activation per source; seeded draw over graveyard creatures in a fixed order; viewer `gyreturn=@random`. | random = deterministic seeded value the search sees (Gamble model) — PROVISIONAL |
| (fold fix) Haven of the Spirit Dragon / Fengraf | — | the end-of-turn dominance fold + solve key now distinguish graveyard creature names; Dragons byte-identical. | — |
| Genesis Wave (existing) | — | landfall in pass 2; CR 603.3b dominance trigger order (landfall decks only); left-land still fires; duplicate legend now PUT. | — |
| Mountain / Island (existing) | — | Basic supertype added; `audit_card_fields.py` hard-checks Basic. | — |

Land-search branch `landfall-search` (563b1e25, c793dd25, d9101f93; 2026-10-15 ~17:00):
- tests: unit 633/633 (17 new), scenarios 174/174 (9 new `landsearch_*`), smoke `play-changed=0`, ALL PASS;
- land picks: every legal complete pick, provider-ordered, no narrowing; per-ordinal pins (full product
  <= 64, one-hot beyond); state pins for Titan's attack trigger and Search for Tomorrow's arrival;
- viewer: `tutor_etb` per land, a `landsearch=` verb, `dig` for the in-combat crack, `suspend=`;
- design + gaps: `docs/design/land-search-cluster.md`;
- cost warning: 24 games at d3/50 ms ran 5m17s on 12 threads (contended) — Titan's ~25-wide pick axis
  is the prime suspect for the 5f performance gate.

Commit `078acb9e` (2026-10-15 ~15:00): unit 616/616, scenarios 165/165, viewer linebuild 0 fail over
467 refs, smoke `play-changed=0` at every depth, ALL PASS. Contract + deferred items:
`docs/design/landfall-trigger-infrastructure.md`. Landfall currently routes to SelesnyaLifegainProvider
(keyed on Genesis Wave, an empty derivation) — Stage 4a gives it its own provider.

Remaining land-timing gaps (handed to the Courser integrator, 2026-10-15 ~15:05): a land in the MIDDLE
of the cast order (cast Khalni, play the land, cast Rampant Growth with its mana) and land-timing
inside breakpoint continuations. CLOSED 2026-10-15 by `Plan::land_slot` (the land's position after the
k-th landfall-watcher cast, in the main plan and inside continuations) -- see
`docs/design/landfall-trigger-infrastructure.md` section 3 and deferred item 1.

Courser + land-position commits on `landfall-analysis` (2026-10-15, ~17:35):
- 24754d72: Courser play-from-top (`Plan::land_from_top`).
- 8bfdafbc: `Plan::land_slot` — 0 first, -1 after all casts, k after the k-th landfall-watcher cast;
  the `landat=<k>` verb.
- 17a5525f: land positions inside breakpoint continuations (held continuation land, lockstep).
- 43f6082b: a land Aura aimed at a deferred land is now cast once the land is played (it was silently
  dropped before).
- Tests: unit 630/630, scenarios 175/175, viewer linebuild 0 FAIL over 467 refs + 2 new checks; smoke
  `play-changed=0`, ALL PASS after every commit.
- Indicative cost on the partial tree (no land-search cards): +9.9% units, +6.5% wall vs 078acb9e;
  position variants were 11.7% of enumerated plans.

### Research verdicts
- **Courser of Kruphix** (agent C, 2026-10-15): live Oracle (all 9 printings) is "Play with the top card
  of your library revealed." — goldfish-equivalent to "look at". Design in
  `logs/landfall_drafts/_courser_design.md`:
  - new params `library_top_revealed` / `play_lands_from_library_top`;
  - one land body `PlayLandFromLibraryTop` sharing `PlayLandFromHand`'s path, so landfall fires;
  - a `land_from_top` source bit on Plan / PlayLand / line spec, carried by every land call site and every
    dedup key;
  - hand-vs-top is branched, with no sound fold (the library cursor changes every later draw);
  - a top change mid-turn (shuffle / Wave / Courser entering) arms the hand-entry re-solve plus a
    `PlanOpensBreakpoint` clause (else canon greedy);
  - the greedy sites get `GreedyLandChoice` top awareness;
  - viewer: a `library_top` board field and a `landtop=` line verb (a main_phase plan variant, bucket A).
- **Land timing gap** (found by agent C): a Plan always plays its land BEFORE its casts (only Karoos
  defer), so "cast the landfall payoff, then play the land" was inexpressible. Added to agent A's
  scope 2026-10-15 ~14:20 as a searched land-timing axis, active only when a same-plan cast carries
  a landfall watcher.

- **Utility lands + existing-card review** (agent D, 2026-10-15; full text
  `logs/landfall_drafts/_existing_cards_review.md`):
  - Radiant Fountain: Tier 1 (`produces C`, `etb_lifegain 2`); not basic.
  - Haunted Fengraf: Tier 2. It reuses Haven of the Spirit Dragon's `gy_return_*` params plus a new
    `gy_return_at_random`: one activation per source, with the random pick a deterministic seeded hash
    the clairvoyant search reads (Gamble-style). The `DeckGraveyardReaders` fold must learn that
    gy-return reads the graveyard (Haven has the same hole).
  - Mountain: missing the Basic supertype (Island too) — fixed with a shared `IsBasicLandCard` predicate
    (handed to agent A).
  - Gruul Turf: the put-Karoo bounce takes the provider's first pick (searched only when played from hand).
  - Genesis Wave:
    - (a) refuses a duplicate legend; putting it weakly dominates;
    - (b) pass-2 triggers resolve in reveal order;
    - (c) a land that already left loses its landfall trigger;
    - (d) Phylath's target is fixed at trigger time.
    (a)-(d) were handed to agent A.
  - Terastodon: the user's K rule (`ProjectEtbDestroyK`) never sees its Craterhoof windows here, ignores
    next-turn landfall growth, and destroys a Forest before a colourless land. KEPT (user-owned); measure
    `MTG_UNPRUNED=terak` in 5e.
  - Legend rule: `LegendKeepIndex` keeps the oldest copy (sound for Phylath); the binned copy counts as
    dying. Omnath's trigger reuses `dies_watch_subtype` (the Pashalik Mons shape).

- **Land-search cluster** (agent B, 2026-10-15; `logs/landfall_drafts/_land_search_design.md`):
  - Evolving Wilds / Terramorphic must be REAL lands with a searched activation (`template: custom`),
    not the fetchland fold, which loses a landfall and the hold line.
  - One `land_search_*` param block plus `PerformLandSearch` (EnterLand per land, one shuffle even on
    fail-to-find).
  - Per-ordinal land-pick pins (the `sac_pins` pattern).
  - Suspend needs a sorcery branch plus a `suspend_cost` param.
  - Titan's attack trigger needs a both-worlds attack-trigger helper and an attack-projection addend.
  - "Up to" / "you may" collapse is sound because the shuffle orders by card number (removing an extra
    land leaves the rest's order unchanged).
  - Gaps: no in-combat activation window (Khalni filled by Titan's attack can't be cracked before
    damage); activations always run after casts.
  - **Found an existing in-window greedy in another deck**: Crop Rotation's land-target axis is dead
    (`PerformLandTutorToBattlefield` ignores the searched pick) — Creature Giving. Separate item; its
    fix moves Creature Giving GT.
  Integration: a parallel worktree `landfall-search` (agent, started ~14:35) that routes put lands
  through `EnterLand` and is rebased onto `landfall-analysis` once agent A's work is committed.

## Stage 3 — coverage re-check (2026-10-15 ~18:15, combined tree b6e29c62)
`missing: []`, no gaps. Every card's bracket note is an implementation description; the real
PROVISIONAL deferrals are listed in the card table above. (Tool trap: `audit_viewer_decisions.py`
and `audit_card_costs.py` resolve `src/cards/data/cards.json` relative to the CWD — run them from
the worktree root, or they silently audit the primary checkout's cards.json. Found when the viewer
audit matched only 5 of 20 cards.)

Static viewer audit (`--no-sweep`): expected decision types `bounce`, `dragon`, `target`, `tutor_etb`;
oracle cross-check advisory only for Omnath's "any target" (disclosed PROVISIONAL: always face).

## Stage 4a — provider (2026-10-15, be6f475d)
`LandfallProvider`: an EMPTY DeckProvider derivation (play-neutral; Landfall had been captured by
SelesnyaLifegainProvider, also empty, via Genesis Wave's `reveal_x_put_permanents`). Signature = the
landfall-payoff params across five cards, routed above `selesnya`; it excludes the land-search params
(Hydras runs Search for Tomorrow), Courser's params, and Searing Blaze's `landfall_damage`.
`Certificate()` = NotAssessed (names what a bound must price). `provider_audit.py --check` rc 0; the
routing pin test passes.

## Stage 4 — baseline profile
Launched 18:26 (detached; log `logs/landfall_profile.log`); DONE — see "Stage 4 — baseline profile
(DONE ...)" below.

## Stage 5f — performance gate
Suite gate reference: Snow 7,162 ms/game, so the new-deck budget is 21,486 ms/game (max per-game core-ms
over the regression tier's searched cases). Early evidence (12 games d3 b20, contended): ~37 s/game
mean, with one 257 s game (Genesis Wave: 13 -> 44 permanents). Likely OVER the 3x line, which makes
performance the FIRST goal (CLAUDE.md). A perf agent in worktree `landfall-perf` (from be6f475d)
profiles the heavy games and ships only sound collapses.

Perf pass DONE (cherry-picked onto `landfall-analysis` as 91d01347..3aa12384; full write-up
`docs/design/landfall-perf.md`): five byte-identical commits — land-search pick-list memo, per-node
trims, combat-pin variants share the base plan's main-phase apply, and its dedup key, Karoo colour
masks once per call. Callgrind Ir on three fixed games: -7.9% / -6.8% / -51.6%; pooled sample 0.64-0.68x
core-seconds, digests and units identical; smoke ALL PASS `play-changed=0`. Then `search_leaf_depth: 0`
ADOPTED in the profile (223cbba4; cheaper and better on held-out seeds -0.141 turns, se 0.031, 62/23
paired; surfaced to the user for sign-off). Merged tree: unit 649/649, scenarios 184/184.

## Investigation — Search for Tomorrow suspend (2026-10-15 ~18:35)
- Probe: 24 games, d3 b20, seed 9000 (`logs/sft_probe/`): avg 5.75, all won; SLOW-GAMEs gi 11/13/17
  at 130-198 s wall, 1.7-2.0M units.
- Observation: SfT was NEVER suspended. In g12/g16/g19 the T1 board was Forest + SfT in hand and the
  search played the Forest and left {G} idle.
- Suspend IS offered: the claude-play T1 menu for seed 9019 lists plan 1 "land=Forest; cast: Search for
  Tomorrow (suspend)".
- Forced A/B via `--choices-then-auto` (d3 b20): Forest-only and Forest+suspend BOTH win T7, so in this
  game the indifference is not provably wrong.
- OPEN: whether a T1 suspend ever yields an earlier win is the 5g earliest-win miner's question
  (MTG_DUMP_EWINS inclusion rule for "Search for Tomorrow (suspend)") — queued for when the box is free.

## Stage 2d-bis — cost / field audits (2026-10-15 ~18:50)
- `audit_card_costs.py` (run alone): 307 of 427 costed cards compared before Scryfall 429'd the rest. ALL
  20 Landfall cards fell in the uncompared tail (newest entries sit last), so the script result is a
  DID-NOT-RUN for this deck. Verified BY HAND instead (curl, 0.6 s spacing): all 20 deck cards match live
  Scryfall on mana_cost, P/T, supertypes+types and keywords; oracle text (before the bracket note)
  matches on 19. Mountain's oracle lacks the reminder parentheses — cosmetic, fixed in the next data
  commit.
- One cost MISMATCH, pre-existing and not in any shipping or archived list: Primal Vigor ({4}{G}{G} vs
  Scryfall {4}{G}; a Fungus screening candidate from 85f6cd4a) — fixed (zero play impact).
- `audit_card_fields.py` (offline): 3 HARD mismatches, all pre-existing and none in this deck: Primal
  Vigor (above), Saproling Migration (keywords lack kicker), Kíli the Resourceful (keywords lack
  storied; KittyEquipment). The last two need keyword-enum support and Kíli is in a suite deck —
  surfaced, not fixed. "oracle_text diverges" advisories on this deck's cards are the long bracket notes.

## CI (2026-10-15 18:40, branch `landfall-analysis` @ 5bd2c45e)
Run 38787489105: build (windows-latest) success, build (ubuntu-latest) success, Linux/Windows
determinism parity success.

## Stage 4 — baseline profile (DONE 2026-10-15 ~22:10, commit 5b72e5e2)
`analyze_deck.py --no-rebuild --analyzer-seed 151015` on be6f475d: 1000-game d5 card scores; cost
diagnostic `NO_COST_INTERACTIONS`; discard evidence (400 games d3) `STATUS_QUO_OK` — 9 cleanup sheds
in 400 games (0.0225/game), multi-optimal rate 1.0 (every shed a tie), names offered: Baloths 7,
Phylath 7, Titan 6, Avenger 5, Omnath 5.

## Stage 5j — RE-MEASURED: cost gate FAIL 4.30x (2026-10-16 ~05:00, tree b35f9f0e) — deck NOT added
The 10-15 PASS below compared a fresh Landfall run against a STALE table (Snow listed at 7,162 ms/game;
in the same pooled run Snow is 2,379). Re-measured the established way (PD's admission: "1.33x hinata in
ONE pooled batch"): the full regression tier on the integrated tree, THREADS=24, every other case
byte-identical to GT, then `suite_gate.py --measure-all --no-run` (table refreshed, 09ba5608) and
`--cost decks/Landfall --no-run`: **landfall 22,919 ms/game (d5 s2002) = 4.30x the reference, PD at
5,335 -> FAIL** (budget 16,006). Per case: d5 s2002 22,919; d3 s2002 12,758; d5 s3003 11,518; d3 s3003
8,237 (the gi38 runaway is gone: d3 s2002 48.4M -> 10.6M units). Rows NOT added (proposed diff kept at
`logs/landfall_suite_rows.proposed.diff`; shape as recorded below).
- **WHERE THE COST IS: mulligan-bottoming trial games, 92-99%.** Turn census on the three heaviest d5
  games (s2024 gi22, s3013 gi10, s2022 gi20): probe (trial-game) units 99.0% / 97.1% / 92.5%; the real
  game 1.0-7.5%. Corroborated by the curve-check A/B: with `curve_check none` (far fewer mulligans) the
  same games cost ~0.25x units. This is the PD / Bruna / FiveColour / Fluctuator shape (68-92%), where
  every prior analysis found no lossless reduction (PD P4: the cost is in candidates that TIE the best).
- **The remedy every reference deck already has** is the exhaustive keep/bottom table (it replaces these
  rollouts outright: `AIEngine::BottomCards`' table path runs first) plus the value leaf -- and both
  generators refuse a deck outside the suite. The gate therefore compares Landfall's PRE-artifact cost
  with the peers' POST-artifact cost; play alone is ~1-7% of these games.
- **USER DECISION (PD's D1 shape; default taken: nothing adopted that costs quality, deck in NO tier):**
  (1) lift the gate for Landfall (`MTG_ALLOW_UNTESTED_DECK=1`, user-only) to build the keep table + value
  leaf; (2) rule that 5j measures post-artifact cost; (3) ship cheaper bottoming (`bottom_eval_*`, lossy;
  measured below). Recommendation: (2) via (1), as for PD.

- **Option (3) measured (2026-10-16, held-out seeds 1.0M-1.3M x 250 = 1,000 paired games, PLAY settings
  d5 b20, tree b35f9f0e, one pooled batch with the 5c2 check):**
  | arm | turns vs shipped | worse / better | units vs shipped |
  |---|---|---|---|
  | `bottom_eval_depth 0, topk 5` (Melira shape) | **+0.036 (se 0.0067)** | 37 / 3 | 0.59x |
  | `bottom_eval_depth 0` (all-greedy trials) | **+0.126 (se 0.0122)** | 118 / 4 | 0.19x |
  Both cost quality clearly, in one direction (the trials are an oracle on the true draws), as on PD.
  The Melira shape would land near the 3x line (~0.6x of 22,919) at a cost of +0.036 turns/game. NOT
  recommended; NOT adopted.

## Stage 5j — suite membership: cost gate PASS (2026-10-15 ~23:00, tree 223cbba4) — SUPERSEDED above
Rows written in all three tiers (smoke d0 1001x1000, d3 1001x50 b10, d5 1001x25 b20; regression d0
2002x1000, d3 2002/3003x50 b10, d5 2002/3003x25 b20; overnight ENTRY set: d0 4x2000, d3 4x400 b10,
d5 4x300 b20). No 2HG row (no per-opponent or starting-life-relative card). `suite_gate.py --cost`
(THREADS=12, host load ~20-25): **18,512 ms/game, worst case `landfall_regression_d3_s2002`, = 2.58x
Snow (7,162) -> PASS** (limit 3x). Per case (ms/game): d5 s3003 5,624; d5 s2002 12,175; d3 s3003 4,514;
d3 s2002 18,512.
- **The worst case is ONE game.** d3 s2002 gi=38 (repro `--seed 2040 --game-index 38 --games 1 --depth 3
  --budget-ms 10 --ignore-play-profile`) ran **606 s and 38.6M units** at a 9,000-unit decision budget
  (the 25x overrun ceiling is 225k per decision). Without it the case is ~6.4 s/game and the deck's
  worst case is d5 s2002 at ~1.7x. Under investigation (below): a single decision runs for minutes.
- **ROOT-CAUSED AND FIXED (2026-10-16, db77ee91 on this branch; analysis in `landfall-perf.md` §6).**
  The runaway was a mulligan-bottoming TRIAL game's turn-1 fallback `SolveWithLookahead` (no verified
  win to commit, so the executor re-solves on a fresh budget). That host never armed FullSearchLine's
  25x proportional overrun ceiling, so ONE root candidate's depth-2 rollout (turn 7 x turn 8 re-solves
  of 21-62k plans each: Titan attack pins x in-combat Khalni cracks x land-search pins) ran 37.9M units.
  Every pass after the first now arms the same ceiling and rolls back to the last COMPLETED pass
  (keeping a strictly better proven win); pass 0 is never armed. It only re-prices work: inert at an
  unlimited budget, same constant. gi38: 38.6M -> 0.94M units, identical realised game. Unit 649/649,
  scenarios 184/184, smoke 119/119 and regression 166/166 byte-identical. Not deck-specific and not
  leaf-depth-specific: at leaf 1 a different trial ran a 79.5M-unit root, and the same hand forced in
  the REAL game (probe=0) ran 37.9M units / 1,410 s. `MTG_LA_PASS_CEILING` (default ON, `=0` restores
  the uncapped pass) is the A/B arm. The cost gate is re-measured after the remaining fixes land.
- Rows and GT are NOT committed yet: GT is accepted once the two gap fixes (Search for Tomorrow mana,
  fetched-Karoo bounce) land, so the deck enters with the play it will keep.

## Stage 5a/5b — mismatch harnesses + depth sanity, EARLY run (2026-10-16 ~02:00, tree f0758952)
Pre-integration run (runaway fix in; the land-search gap fix and the sweep-flag fixes NOT yet), pooled
per arm as `verify_deck.py` does, seeds 2002/3003/7001/7002 x 50 games, b20. `verify_deck.py` re-runs
the gate on the final tree.
- **nonconv (d3): 0 lines. fd-diverge (d5, MTG_FULL_DEPTH): 0 lines.** 400 games, both arms rc=0.
- Depth sanity (avg win turn, all 50/50 won): d3 5.82 / 5.92 / 5.86 / 5.86; d5 5.76 / 5.86 / 5.84 /
  5.84 -- d5 <= d3 on every seed; d0 (regression s2002) 7.667. Monotone, and a T5-T6 clock is the
  deck's realistic ramp-into-Titan/Avenger line.
- Slowest games (d5): s2002 gi38 115 s / 3.5M units, gi22 97 s; all under the pass ceiling now.

## Stage 5c2 — horizon-honest tie-break: NO SIGN at this sample (2026-10-16, tree b35f9f0e)
`leaf_tiebreak_check.py`'s manifest (4 blocks x 250 at PLAY settings d5 b20, both arms per-job, pooled
with the bottoming A/B): **2 changed games of 1,000 paired (0.2%), both BETTER with the tie-break (net
-2), 0 worse.** Below the 20-change floor, so no direction is claimed; the script asks for ~21 blocks
(~5,250 paired games at d5). Landfall wins well inside the horizon (T5-T6 at d5), so the leaf rarely
binds. Default ON stays (a lever that does not fire costs nothing). The decisive run is DEFERRED to after
the keep table, when the bottoming trial games (92-99% of today's cost) are a table read and the sample
is ~20x cheaper -- recorded, not skipped.

## Mulligan keep rule — `curve_check: two_drop` measured (2026-10-16 ~03:00, input for the USER's decision)
The Stage 4 profile keeps the engine-default `two_drop` curve check: at mulligan 0/1 a hand needs >= 2
lands AND a nonland of MV <= 2 (`AIEngine.cpp` KeepHand). In this 60 only Rampant Growth and Khalni
Heart Expedition qualify (8 cards; Search for Tomorrow is MV 3), so ~34.6% of 7-card hands fail it on
that clause alone. Paired A/B, `two_drop` vs `none`, held-out seeds 51000-54000 x 125, d3 b10, tree
6d08ec65: **`none` is WORSE by +0.218 turns (se 0.035), 104 games worse / 36 better / 360 tied**
(two_drop 5.94 avg vs none 6.16). It is also ~3-4x the search cost (each mulligan runs the lookahead
bottoming trial games). Recommendation: KEEP `two_drop`; the keep table (mulligan generation) supersedes
it later. Mulligan policy is the user's — surfaced, unchanged.

## Land-search gaps 2 and 3 CLOSED (2026-10-16 ~04:00, cherry-picked 0ee8f5c5..1528ebc7)
- **Gap 2 (Search for Tomorrow hard-cast):** the untapped basic now funds later casts in the same main
  phase, identically in the search's apply and in the executor (`Action::rock_mana` ->
  `StackEntry::land_put_credit`); nothing is offered when no basic is left.
- **Gap 3 (a PUT Karoo's bounce):** searched for Primeval Titan ETB / attack and Genesis Wave (an
  extended land-search pick list + `Plan::bounce_choice` as the Wave's pin, sized by
  `PutKarooBounceBound`). Plus the put-bounce fold (`MTG_PUT_BOUNCE_FOLD`, default ON, exact: a
  check-only mode verified 4,875,783 would-be skips were all skipped by the existing fold too, and the
  verify run matched fold-off exactly).
- Agent-side validation: unit 662/662, scenarios 184/184, smoke 119/119 byte-identical for every other
  deck; SelesnyaLifegain (Genesis Wave + Selesnya Sanctuary) identical results. Landfall, 240 paired games
  (s8008/s9009, d3 b10 + d5 b20): 5.9250 -> 5.9167, 4 better / 2 worse, units 1.020x, wall 1.100x.
  The one slower game (s8008 gi20, 6 -> 7 at d3 and d5) recovers to T6 at depth 8 unbudgeted. These are
  capability additions (lines that were unfindable at any budget), not narrowings.
- Integrated tree (runaway + sweep flags + gaps): unit 671/671.

## Claude-play sweep
- commit: `223cbba4` (frozen checkout `.claude/worktrees/landfall-sweep`, binary copied before any later
  rebuild), benchmark d5 b20 with the shipped profile (search_leaf_depth 0)
- seeds: 31000+gi, game-index gi, gi 0..15 (disjoint from the suite) games: 16 — one Opus player per game
  (CLAUDE.md: model on judgement, not price); per-game table `logs/sweep_tally/results.md` (gitignored)
- win turns: 15 ties, 1 Claude-earlier (gi6: Claude T5 vs AI T6). gi6 root-caused by its player as BUDGET
  STARVATION on the T1 decision (5c): at b20 the search cracked Terramorphic instead of suspending Search
  for Tomorrow; the same seed at b200 plays the suspend line and wins T5, and Claude's T1 handed to the
  shipped 20 ms search (`--choices-then-auto`) also wins T5. Not a capability gap. (gi15: the AI's own
  line double-suspended SfT — suspend IS chosen by the search.)
- every state transition the players checked was correct (Plant counts per basic, landfall counters,
  quest counters, Genesis Wave simultaneous landfall/ETB counting, Titan ETB/attack fetch lists, the
  in-combat Khalni crack, Fountain life on put and play, Myriad's shared-type rule, suspend timing).
- flags (deduplicated; the counts are games that reported them):
  1. **land_slot k with an UNPAYABLE prefix** (13/16, two players filed it as a bug): a "land after the
     k-th watcher" clone is offered when the casts before the land need that land's mana; picking it
     drops the cast (`dropped_casts`) — and in gi10 the rest of the plan still ran (Phylath dropped, the
     4-counter Khalni still cracked: the shown line silently became a different one). TurnSolver's clone
     site documents it as "does less, scored as one". Illegal human menu entry + wasted search. FIX
     queued (prune, sound: such a plan is a land-first plan with fewer casts).
  2. **land_slot k duplicates land_after_casts** when the k-th watcher is the last cast (7/16):
     byte-identical states, different dedup signatures (`|at-1` vs `|at<k>`). FIX queued (identity fold).
  3. **Terastodon's destroy victims are never offered to the HUMAN** (gi12): the menu fans K only;
     `DefaultEtbDestroyVictimClass` (the user's Forests-first rule) picks silently although Myriad /
     Mountain / Turf were legal and not fungible with a Forest. FIX queued (viewer surfacing; the
     search-side rule stays the user's, open Review Q2).
  4. Terastodon plan summaries omit K (gi5, cosmetic). FIX queued.
  5. Khalni in-combat crack prompt: `heuristic_default` 0 ("crack now", labelled the AI's pick) while the
     AI held it, and generic dig wording (gi10, cosmetic). FIX queued.
  6. DISMISSED (disclosed): a multi-land search fires each land's landfall as it enters rather than after
     the whole ability (10/16 noticed). `LandSearch.h`'s LANDFALL contract: every landfall payload in the
     pool commutes and none creates a permanent another landfall trigger could target, so the outcome is
     identical; the "one simultaneous event" comment refers to the lands' OWN enter triggers.
  7. KNOWN gaps met: (1) SfT hard-cast mana (gi4, gi15) — fix in flight; (2) Titan-put Turf bounce — the
     HUMAN side was verified correct (gi1, gi12, gi15); the gap is autonomous-only, fix in flight.
- RESOLUTIONS (2026-10-16, cherry-picked d4e6a424..8aebcaab): (1) unpayable-prefix positions not emitted
  (`SlotPrefixProbe`; kept where a later choice can add mana back and inside breakpoint continuations;
  CheckLine grades such a `landat=<k>` line Illegal); rollout and executor verified to agree (both drop
  the cast and still play the land; no phantom quest counter). (2) the k==-1 twin folds at the
  post-apply dedup, and the human menu hides the exact twin. (3) the human picks each Terastodon victim
  (`target`, default = the user's Forests-first order; the SEARCH rule is unchanged). (4) "(destroy K)"
  in summaries. (5) the crack prompt defaults to keep, with crack wording. Smoke byte-identical for every
  other deck; Landfall A/B 160 games, all tied, 0.968x units. Residuals deferred in
  `landfall-trigger-infrastructure.md` (items 7-8).
- flags: 0 unresolved

## Stage 5i — discard BUCKET policy (AUTHORED, NOT ADOPTED — presented to the user)
Evidence first: the deck sheds at cleanup ~once per 44 games and every observed shed was a tie, so the
measurable prize is ~0; the policy is doctrine quality plus rollout fidelity. Proposed shape (USER
doctrine 2026-08-21: quota-first, net of board, fungible upward, distance-to-playable, one index):

| bucket | members | keep quota (net of board) |
|---|---|---|
| Mana / LANDS | every land (Forest, Mountain, Wilds, Terramorphic, Myriad, Fountain, Turf, Fengraf) | land drops: 2 while board lands + ramp < 7, else 1; a red source is kept first while neither board nor hand has one and the hand holds Phylath/Omnath |
| Mana / RAMP | Rampant Growth, Search for Tomorrow, Cultivate, Khalni Heart Expedition | kept while board lands < 6 (the 6-drops); fungible with LANDS (parent total binds) |
| Threats | Avenger, Primeval Titan, Rampaging Baloths, Phylath, Omnath, Terastodon, Genesis Wave, Courser | floor of 2; rank (best kept first): Avenger > Titan > Baloths > Phylath > Genesis Wave > Omnath > Terastodon > Courser |

Shed order inside an over-full bucket = distance-to-playable (board+hand mana vs the card's cost, colour
included: Omnath needs RR, Phylath R), so a 7-8 drop on a 3-land board sheds before a castable 6-drop.
Courser ranks last among threats because it is the only one that is not a payoff (it plays lands and
gains life); Genesis Wave sits mid-rank because its X scales with exactly the mana this deck makes.
Doubts flagged: (1) the threat rank is my judgement from card text, not mined (the 5g miner's
INCLUSION report is the evidence to re-rank on); (2) Khalni is filed under RAMP though it is also a
landfall payoff — it only pays off by being cracked for lands, so RAMP is its role. NOT implemented:
per the skill it ships only on approval, as `LandfallProvider::CleanupDiscardCandidates` behind a
default-on `MTG_LANDFALL_BUCKET_DISCARD` flag, then held to non-inferiority on all three tiers.

## Stage 6a — encoded heuristics & assumptions disclosure (2026-10-16, tree b35f9f0e)
Compiled from the CODE (a read-only agent pass at f0758952, then updated for the merged fix branches,
6d08ec65 and the 5j re-measure). Tags: PROVISIONAL = a deferral/collapse on the default awaiting the
user; KNOWN GAP = missing capability, disclosed; UNVERIFIED = not confirmed from code. The verbatim
bracket notes of all 20 cards are in `src/cards/data/cards.json`.

### Source 1 — Global engine assumptions in force for this deck

| # | Assumption / heuristic | Source (file:line) | Classification | What it costs us / why it's safe |
|---|---|---|---|---|
| 1.1 | **Single passive goldfish opponent.** It never blocks, casts, gains or prevents life, and controls no permanents. So every "target opponent / any target" lands on its face. Terastodon can destroy only OUR noncreature permanents. Trample (Baloths, Titan) is inert. | `.claude/skills/mtg-ai.md:34,94,271`; `SpellEffects.h:8781-8788` (Terastodon note) | Global model | This is what the suite measures. Opponent-facing clauses are inert by construction. Self-binding clauses (Terastodon, Karoo bounce, legend rule) are modelled. |
| 1.2 | **Clairvoyant search over a known library.** The search reads the real library order, including after every shuffle. Shuffles are deterministic and keyed per card (`Library::ShuffleByKey`), so removing a card leaves every other card's relative order unchanged. The "up to N" collapses rely on this. | `src/core/Library.h:208`; `LandSearch.h:35-41` | Global model | Courser of Kruphix: the search can play "shuffle, then play the newly revealed top land" knowing the post-shuffle order (ledger Courser Q2, default YES). Haunted Fengraf's "at random" is a seeded value the search reads (1.10 / 2.13). Lookahead bottoming and the searched cleanup discard are clairvoyant too. |
| 1.3 | **Second main: first-main only.** `DeckUsesSecondMain` returns false: no card in the list sets spectacle, aura_swap, pod/convoke, devour, ping_all, lifegain_to_loss, hinata, combat_damage_*, attack_draw, attack-impulse, mana_per_life_gained / attack_trigger_lifegain, or blink. No restricted per-turn main 2 exists for this deck either. | `src/runner/GoldFishRunner.cpp:56-355` | Global model (KNOWN GAP for one case) | Combat creates new resources in exactly one case: Primeval Titan's ATTACK trigger shuffles. With Courser out and the land drop unused, that can expose a playable top land after combat. That main-2 drop is unreachable autonomously (the human viewer has it). This is `landfall-trigger-infrastructure.md` §4 item 6, user Q4. The in-combat Khalni crack (1.12) covers the other combat-resource case. |
| 1.4 | **Suite depths / budgets.** d0 (greedy runner) 1000-2000 games; d3 b10 (≈9,000 units/decision); d5 b20. Smoke s1001; regression s2002/s3003; overnight ENTRY set s4004..s10010. | `test/regression_cases.sh:482-492, 731-736, 1294-1307` (**uncommitted** in the worktree) | Settings | d0 cases measure the greedy policy alone (every provider default in Source 3 decides). **NOT in the suite: 5j FAILS at 4.30x PD** (proposed rows only). |
| 1.5 | **`"search_leaf_depth": 0`, the greedy horizon leaf (Melira Pod precedent).** The commit-the-line search scores each leaf with a pure greedy playout instead of a 1-ply lookahead at every rollout turn. | `decks/Landfall/Landfall.profile.json` (last key); read at `AIEngine.cpp:2429, 3422`; `TurnSolver.cpp:122-128` (`EffectiveFdLeafDepth`); doc `MulliganProfile.h:199-209` | Settings (adopted in `223cbba4`, **surfaced for user sign-off**) | Held-out A/B: −0.141 turns (se 0.031), 62 better / 23 worse, 0.47-0.65x units (`landfall-perf.md` §4.1). Cost: every beyond-horizon turn takes the provider's rank-0 default for every sub-decision (1.6), so those defaults weigh more in leaf scores than at leaf depth 1. |
| 1.6 | **Beyond-horizon / d0 playout = provider rank 0 everywhere** (the permitted greedy). That means: land-search pick 0, Phylath target rank 0, Karoo bounce front, Terastodon K projection, Genesis Wave max X and its put rule, `GreedyLandChoice` for the land drop (top-of-library aware). | `LandSearch.h:42-46`; `DecisionProvider.h:588-601`, `2485-2496`; `TurnSolver` permitted sites per CLAUDE.md | Permitted greedy (horizon / d0 only) | Not a search-window heuristic, but it shapes leaf values (1.5) and the whole d0 tier. See 3.6: Titan's rank-0 pick is alphabetical. |
| 1.7 | **No-win leaf grading: opponent life is the only live term.** The board tie-break terms (`MTG_LEAF_TB_BOARD/_PERMS/_NONLAND`) are default OFF. | `TurnSolver.cpp:5888-5902`; measured `landfall-perf.md` §6.1 | Global model | In the gi38 census, 99.9% of equal-win-turn comparisons also tied on opponent life. When no line wins inside the horizon, leaves that grew the board differently are indistinguishable. That is why the land-search / Titan-attack / crack pin variants rarely discriminate at the last turn (§6.2). |
| 1.8 | **Lookahead bottoming at depth > 0.** Each name-distinct legal removal subset is rolled out as a full clairvoyant TRIAL game at the deck's own play settings. The earliest win wins and `bottom_order: count_first` breaks ties. At d0 the heuristic decides alone. No `bottom_eval_depth/_topk` is set. | `AIEngine.h:144-152`; `AIEngine.cpp:1700-1830`; profile `mulligan.bottom_order` | Global model (clairvoyant) | The trial games are ~99% of what remains of the gi38 runaway. Turn census: 92-99% of the heaviest d5 games' units are these trial games; they are why 5j fails. Cheaper trial settings are a user decision (5j section). |
| 1.9 | **Mulligan keep = the legacy static rule** (analyzer-generated; no keep table, no keep model). Keep at ≤4 cards (`stop_at 4`). Otherwise need 1-5 lands, at least one nonland, and, at mulligan 0/1, `curve_check: two_drop` (≥2 lands AND a nonland of MV ≤2). `hand_score_threshold` = -1e18, i.e. no score gate. No `min_color_sources` (no red requirement for the splash). | `decks/Landfall/Landfall.profile.json` (`mulligan` block); `AIEngine.cpp:893-1068` (`KeepHand`; curve check at `1031-1060`) | **User-owned decision, not yet reviewed** | The only MV ≤2 nonlands are Rampant Growth and Khalni Heart Expedition (8 cards). Search for Tomorrow is MV 3: its {G} suspend does not count. So about **34.6% of 7-card and 40.7% of 6-card hands fail two_drop on that clause alone** (hypergeometric). Evolving Wilds / Terramorphic count as lands for min/max and two_drop. **Measured:** `none` is +0.218 turns WORSE (se 0.035, 104/36) -> keep recommended. |
| 1.10 | **No value leaf, no keep table, no `.eval.json`.** `decks/Landfall/` holds only the decklist and profile. | `ls decks/Landfall/` | State | The value-leaf H-cell ladder guard does not apply. Every rollout is a live rollout. |
| 1.11 | **`MTG_LA_PASS_CEILING` (default ON, `db77ee91`).** Every SolveWithLookahead pass after the first, at a budgeted root, arms FullSearchLine's 25x proportional overrun ceiling. A tripped pass rolls back to the last completed pass, keeping a strictly better proven win. Pass 0 is never armed. | `TurnSolver.cpp:66109-66110`, `66460`; `HeuristicArm.h:368` | Budget re-price (inert at unlimited budget) | gi38: 38.6M → 0.94M units, same realised game. Smoke and regression are byte-identical. It caps the damage but does not fix the start gate's ~7,300x under-estimate (§6.8). |
| 1.12 | **In-combat window modelled only for Khalni Heart Expedition, after Titan's attack trigger** (CR 508.4 point, before damage, both worlds). There is no other instant-speed window: no opponent turn, no end step. | `LandSearch.h:706-800` (`FireAttackLandSearch`, `FireCombatLandSearchCracks`) | Global model | See 2.9 / 2.10 for the per-card instant-speed collapses. |

#### 1b — Search-shape restrictions that bind this deck (constant at ANY budget)

These are engine-wide representation limits, not per-deck heuristics. They are listed because each
one is **constant at unlimited budget**, so it is a question for the user's "completeness at large
budget" invariant (CLAUDE.md, 2026-10-06).

| # | Restriction | Source (file:line) | Classification | Lines it can miss |
|---|---|---|---|---|
| 1b.1 | **In-plan land-search pins: full product only up to 64, then additive one-hot per ordinal.** | `TurnSolver.cpp:53160-53191` (`kLandSearchProductCap`); `land-search-cluster.md` gaps 4-5 | Pruning (representation limit) | Titan's ETB alone is ~25 picks. Titan ETB + any second land search in the same plan (Cultivate 4 splits = 100) is past 64, so a plan where BOTH deviate from the front pick is "reachable only through the breakpoint machinery, not as one variant" (in-code wording). |
| 1b.2 | **One axis at a time.** A land-search pick variant never also carries another axis's deviation: scry, Karoo bounce, Phylath target rank. An in-combat crack variant keeps the Titan's own attack pick at the front. Attack and upkeep pins are additive per ordinal. | `land-search-cluster.md` gap 4; `TurnSolver.cpp:53183-53188` | Pruning (convention) | "Titan fetches a non-front pair AND Phylath's counters go to a non-front Plant", "two attacking Titans both off-front", "Titan off-front pick + in-combat Khalni crack". |
| 1b.3 | **Phylath target: a whole-turn rank policy, width capped at 4 classes.** One rank for every Phylath trigger this turn, into the provider order recomputed at each trigger. Ranks ≥4 are never emitted. Outer plans only; a continuation inherits. | `TurnSolver.cpp:52288-52303, 52379` | Pruning (cap + policy shape) | Per-trigger target splits that no single rank produces. A 5th+ identity class of Plant (e.g., several counter levels, sick and un-sick) is never targeted autonomously. |
| 1b.4 | **Land-search ACTIVATION position twins** only for outer plans that also cast a land-counting ETB (Avenger / Phylath). None inside a breakpoint continuation. | `TurnSolver.cpp:53214-53266`; `land-search-cluster.md` gap 1 (remaining boundary) | Pruning (sound for a plain watcher; open inside continuations) | Crack-before-a-land-counting-cast inside a continuation (e.g., after Cultivate's put-in-hand breakpoint). |
| 1b.5 | **Cast ordinals sized at the widest cast source; a narrower source clamps.** Packed attack-pin width is 6 bits (≤64 picks per ordinal). | `TurnSolver.cpp:53174-53179`; `land-search-cluster.md` gaps 5-6 | Representation (no line lost; ~4% duplicate variants) | Nothing at the current pool (Titan ~25 < 64). |

---

### Source 2 — Card-modelling simplifications and goldfish-inert collapses

The full bracket note of every deck card is reproduced **verbatim** in the Appendix (generated from
`src/cards/data/cards.json`). This table lists every deferral, collapse or simplification those notes
and the engine take, and whether each is inert.

| # | Card / clause | Source (file:line) | Classification | What it costs us / why it's safe |
|---|---|---|---|---|
| 2.1 | **Avenger of Zendikar:** landfall "you MAY put a +1/+1 counter on each Plant" → always YES. | `SpellEffects.h:7262-7290` (`FireLandfallTriggers`); `audit_viewer_decisions.py:591` | Dominance collapse — **PROVISIONAL** | All-or-nothing. A +1/+1 counter on our own creature is never worse vs a passive opponent. The only mechanic it can hurt (undying) is on no card in `cards.json`. Applies to the human too (not offered). |
| 2.2 | **Khalni Heart Expedition:** landfall "you MAY put a quest counter" → always YES. | `SpellEffects.h:7322-7335`; `audit_viewer_decisions.py:605` | Dominance collapse — **PROVISIONAL** | A counter only feeds the optional activation, with no cap and no punishing threshold (Beastmaster Ascension precedent). Human not asked. |
| 2.3 | **Khalni activation:** "up to two" basics → as many as the library allows. Post-combat windows (main 2 / end step) collapsed. No opponent-turn window. | `LandSearch.h:35-41`; card note | Dominance collapse — **PROVISIONAL** | An Expedition full before combat is cracked in a main phase. One filled in combat is weakly better cracked in combat (same lands, same untap, plus the pump). Human may stop at one (-1). |
| 2.4 | **Rampant Growth:** fail-to-find / "up to one" not offered to the search. | `LandSearch.h:35-41` | Dominance collapse — **PROVISIONAL** | One land now plus one card removed from an otherwise identical CRN future. Human keeps the decline (-1). |
| 2.5 | **Search for Tomorrow:** off suspend, "you MAY cast it" → cast, for the search AND the human. | `SpellEffects.h:15689-15700` (`CastOffSuspend`); card note | Dominance collapse — **PROVISIONAL** | Declining leaves it exiled with no counters and gains nothing; only the shuffle differs. |
| 2.6 | **Search for Tomorrow:** the hard-cast untapped basic is not credited to the same plan's later casts. | card note "KNOWN GAP"; `land-search-cluster.md` gap 2 | **KNOWN GAP** — **CLOSED** (0ee8f5c5 / 8af164b6, merged) | "SfT, then Rampant Growth / Khalni paid with the fetched Forest" is unenumerable at any budget today. |
| 2.7 | **Cultivate:** "up to two" → two. The reveal is display-only. | `LandSearch.h:35-41`; card note | Dominance collapse — **PROVISIONAL** | Corner case: the hand basic can force a cleanup discard. That is our own choice and equivalent to not fetching it. Human may stop early. |
| 2.8 | **Myriad Landscape:** "up to two" sharing a type → two. Its {T} is paid before the {2}. | card note; `LandSearch.h:150-180` | Dominance collapse — **PROVISIONAL** (the {T}-first order is a rule, not a choice) | Ruling 2018-03-16 allows one; the human may stop at one. |
| 2.9 | **Evolving Wilds / Terramorphic / Myriad:** instant-speed activation → our main phase's trailing pass (or a searched position, 1b.4). A crack with nothing left to find is not offered. | card notes; `land-search-cluster.md` collapses §3 | Timing collapse — **PROVISIONAL** | Vs a passive opponent a combat / opponent-turn crack gives the same board: the basic enters tapped either way, and main-1 counters are on the attackers at damage. |
| 2.10 | **Haunted Fengraf:** "at random" = a deterministic hash of (game seed, turn, eligible count) in canonical order, which the clairvoyant search reads (Gamble model). Instant-speed timing collapsed to the trailing pass. Not offered with no creature in the graveyard. | `SpellEffects.h:11100-11125`; card note | Model simplification — **PROVISIONAL** (Review Q4) | No intra-turn lever to steer the draw (keyed on turn and eligible set only). The returned creature can't be cast before our next main either way. |
| 2.11 | **Primeval Titan:** "you MAY search" and "up to two" → always search the maximal pair. Trample is inert. | `LandSearch.h:35-41`; card note | Dominance collapse — **PROVISIONAL** | Human may decline (-1) or stop at one. |
| 2.12 | **Primeval Titan / Genesis Wave putting Gruul Turf:** the put Turf's bounce takes `BounceSearchCandidates`' FRONT, with no plan pin. | card note "KNOWN GAP"; `landfall-trigger-infrastructure.md` §4 item 5 | **KNOWN GAP** (in-window greedy, autonomous only) — **CLOSED** (0ee8f5c5 / 8af164b6, merged) | The human is asked through the ordinary `bounce` chooser (verified in the sweep, gi1/12/15). |
| 2.13 | **Omnath, Locus of Rage:** "deals 3 damage to ANY target" → always the opponent's face. | `SpellEffects.h:9649-9660`; `audit_viewer_decisions.py:1012` | Dominance collapse — **PROVISIONAL** (Pashalik / Slimefoot precedent) | Opponent life is the only clock. Damage to our own permanents only costs us. Even the one self-target with a payoff (an own toughness ≤3 Elemental to re-trigger Omnath) ends with the same 3 to face minus a body. |
| 2.14 | **Courser of Kruphix:** "play with the top revealed" = display only. Landfall lifegain is near-inert (nothing damages us). No autonomous main-2 drop after Titan's attack shuffle. | card note; `landfall-trigger-infrastructure.md` §3b, §4 item 6 | Inert / **KNOWN GAP** (Q4) | The search is already clairvoyant. The main-2 drop is a real missing line; see 1.3. |
| 2.15 | **Radiant Fountain:** 2 life on enter (played or put) is a real gain event but near-inert. | card note | Inert for the clock | Nothing in the deck reads life. |
| 2.16 | **Genesis Wave:** (a) the "any number" put subset is a provider RULE, not searched (3.3). (b) X is the only searched axis and is max-only (3.2). (c) Pass-2 triggers resolve in a fixed CR 603.3b order (enter-watchers + creating/counting ETBs, then landfall per placed land, then board-reading / board-shrinking ETBs and land triggers), landfall decks only. (d) A land that already left still triggers landfall. (e) No shuffle. | card note; `landfall-trigger-infrastructure.md` §1 "Simultaneity" | (a)(b) Pruning; (c) correctness shortcut (dominant order); (d)(e) rules | (c): the controller may order simultaneous triggers. The stated order is weakly dominant (counters reach the Plants made this resolution; Terastodon eats lands after their landfall fired). The human is not asked to order them (4.9). **Stale text (FIXED 6d08ec65):** the note said "In this 60, 53 cards are permanents and 7 are not (3 Feed the Clan + the 4 other Waves)". That is SelesnyaLifegain's list; Landfall's is 48 / 12 (4 Rampant Growth, 4 SfT, 2 Cultivate, 2 Wave). |
| 2.17 | **Terastodon:** the opponent controls no noncreature permanents, so the ETB destroys OUR OWN. K = the user's one-option projection (3.4). Victims = the user's class order (3.5). | `SpellEffects.h:8781-8818`, `8027-8037`, `15025-15182` | User heuristic (KEPT; Review Q2) | **Stale text (FIXED 6d08ec65):** the note said a PUT Terastodon picks K "by the same lethality windows only, no cap fallback". The code routes every put through the same `ProjectEtbDestroyK`, which ends `return kcap` ("go for broke") at `SpellEffects.h:15183`. Also, this deck has no Craterhoof, so the h=0 and h=1' windows never fire. Only h=1 / h=2 / cap remain. |
| 2.18 | **Gruul Turf:** mandatory bounce on enter. {R}{G} from one tap. | card note; `ManaPayment.cpp:507-512`, `3699-3706` | Model | **Stale text (FIXED 6d08ec65, all four Karoos):** the note said "{R}{G}, modelled as wild like other duals". The payability walk (`ManaPayment.cpp:3703`) and the executor tap (`:507`) produce one of EACH colour. The comment at `:509` says `AddSourceToPool` credits it as 2 wild, i.e. an optimistic bound only. UNVERIFIED whether any consumer still pays from the 2-wild credit. |
| 2.19 | **Phylath, World Sculptor:** legendary ×4. A second copy's ETB resolves before the legend rule bins one (the oldest is kept), and that death is an Elemental dying for Omnath. Target fixed at trigger time (CR 603.3d). | card note; `SpellEffects.h:17233-17260` | Rules + keep-oldest (3.10) | The keep choice is near-inert (only sickness differs). |
| 2.20 | **Rampaging Baloths:** mandatory (errata'd "create"). Trample inert. | card note | Inert | — |
| 2.21 | **Forest / Mountain:** the Basic supertype is read via `IsBasicLandCard`, never the `basic_land` template (Gruul Turf, Myriad, Fountain and Fengraf carry that template but are not basic). | card notes; ledger row "Mountain / Island" | Rules | — |
| 2.22 | **LANDFALL contract — per-land sequential firing.** A multi-land put (Myriad, Khalni, Titan, Cultivate's battlefield half) calls `EnterLand` once per land, so each land's landfall fires as it enters rather than after the whole ability. | `LandSearch.h:48-54`; `landfall-trigger-infrastructure.md` §1 | Commutation fold (sound for the current pool) | Every landfall payload in the pool commutes, and none creates a permanent another landfall trigger could target, so the outcome is identical. The lands' OWN enter triggers (Turf bounce, Fountain life) run in a second pass, after all lands are on the battlefield. A future payload that makes a target-subtype permanent breaks this. Sweep flag 6 dismissed on this argument. |
| 2.23 | **Land-drop POSITION only after a landfall-watcher cast** (`Plan::land_slot` ∈ {0, -1, k after the k-th watcher}). | `TurnSolver.cpp:54555-54580`; `landfall-trigger-infrastructure.md` §3, §4 item 2 | Identity fold (sound for this pool) | Moving the land past a non-watcher cast triggers the same watchers and gives its mana to fewer casts. Breaks if a future NON-watcher cast makes a landfall target subtype, or a lifegain watcher is cast beside Courser. |
| 2.24 | **Last-known information** for a watcher that leaves mid-cascade is not modelled. | `landfall-trigger-infrastructure.md` §4 item 3 | KNOWN GAP (unreachable today) | No landfall payload removes a permanent. |
| 2.25 | **Copies / behavioural twins fold in land picks.** Evolving Wilds == Terramorphic Expanse (`behaviour_identity`). | `LandSearch.h:31-34`, `64-125` | Identity fold | Which physical copy is taken is visible only through clairvoyance. |
| 2.26 | **Lands with no mana ability** (Evolving Wilds / Terramorphic) are `template: custom`. A few `IsLand()`-based heuristics still count them as lands: keep-for-main-2 count, end-of-main sweep, anti-lifegain tutor count, and the mulligan's land count (1.9). | `land-search-cluster.md` gap 7 | Optimistic approximation | Optimistic, not wrong. Most visible in the mulligan rule. |

---

### Source 3 — DecisionProvider heuristics

**Routing.** `SelectDecisionProvider` (`DecisionProviders.cpp:14303`) routes the deck to
**`LandfallProvider`** (`DecisionProviders.h:2378`; selected at `DecisionProviders.cpp:14168`). The
signature is the landfall-payoff params (`DecisionProviders.cpp:13702-13717`).

**`LandfallProvider` overrides NOTHING.** It is an EMPTY `DeckProvider` derivation: only `Certificate()`
(NotAssessed) and `Name()`. Every judgement hook is therefore the generic one (`GenericProvider` →
`DecisionProvider` defaults): no deck-specific heuristics. The table lists the inherited generic hooks
that **actually fire on this deck's cards**, because those are what shape its numbers. (Not reached:
`FetchCandidates` / `TutorCandidates`, since the deck has no fetchland or tutor params; scry and dig
hooks; `OfferDuplicateLegendCast`, see 3.11.)

| # | Hook (inherited) | Source (file:line) | Classification | What it decides / lines it could miss |
|---|---|---|---|---|
| 3.1 | **`CastOrderRank` (Generic)**. Within a main phase, casts are stable-sorted by rank: creatures 10 (Baloths, Avenger, Titan, Phylath, Omnath, Courser, Terastodon) before noncreature spells 20 (Rampant Growth, SfT, Cultivate, Khalni, Genesis Wave). Ties keep plan (hand) order, except that a positioned-land plan puts landfall watchers first (`LandSlotWatchersFirst`, ledger Courser Q1). Cast-ORDER search is off for this deck. | `DecisionProviders.cpp:2978-3150`; `ManaPayment.cpp:2456-2516` (`CastOrderLessRanked`); `TurnSolver.cpp:44670-44682` (`OrderingSearchEnabled`: `WantsCastOrderingSearch` false); `TurnSolver.cpp:19597-19601` | **Pruning — USER-OWNED cast order, none authored for this deck yet** | **Misses "ramp spell, THEN a land-counting ETB" in one main phase.** Examples: Rampant Growth / Cultivate / SfT before Avenger (one more Plant per land) or before Phylath (one more Plant per basic). Only land-search ACTIVATIONS got position twins (1b.4). Spells did not. The reverse order (creature first, so its Plants get the landfall counters) is the only one played. UNVERIFIED: whether a Cultivate put-in-hand (site 10) continuation can re-admit an Avenger after Cultivate. |
| 3.2 | **`XCandidates` max-X (Generic)**, applied to Genesis Wave: one variant, X = max affordable from the WHOLE pool. | `DecisionProviders.cpp:3183-3197`; `TurnSolver.cpp:22415-22465` | **Pruning** (`MTG_UNPRUNED=xspell` is the A/B) | **Misses "cast a creature, then a smaller Wave" in one main phase**, because max-X leaves no mana for anything else. Phylath (4) then Wave X=3, where each Wave land puts +4 on a Plant, or Avenger then Wave: never enumerated. In-code justification: "a bigger X is strictly deeper"; the counter-argument ("holding mana for a second spell") is named in code and untested. |
| 3.3 | **`GenesisWavePutPicks` (base)**. Put every legal revealed permanent except a creature whose MANDATORY ETB destroys our own permanent. A duplicate legend IS put (2026-10-15). A Karoo IS put. | `DecisionProvider.h:2638-2694` | Decision rule, not searched (a searched subset is 2^X) | Misses deliberately leaving a permanent in the graveyard. For this pool, everything is weakly better on the battlefield. Terastodon is put, and its K projection may choose 0. Ledger Review Q3 (default YES). |
| 3.4 | **`EtbDestroyK` = `ProjectEtbDestroyK` (base, USER rule 2026-08-20/21)**. ONE projected K at resolution, for the cast and every put: the earliest lethal horizon, then the smallest K. No searched fan (human play / `MTG_UNPRUNED=terak` fan 0..cap). | `DecisionProviders.cpp:699-702`; `SpellEffects.h:15025-15182`; emission `TurnSolver.cpp:24086-24104`; resolution `SpellEffects.h:8790-8818` | **Pruning — USER-owned rule, KEPT** (ledger Review Q2) | Here the h=0 / h=1' windows are dead (no Craterhoof). The projection ignores next-turn landfall growth (Plants, Beasts, Elementals) and Elephant-vs-landfall trade-offs. Ledger plan: measure `MTG_UNPRUNED=terak` in 5e. |
| 3.5 | **`EtbDestroyVictimClass` (base, USER order)**. Forests (class 0) before every other land (1: Mountain, Myriad, Fountain, Fengraf, Turf, Evolving Wilds), then mana rocks (2), other noncreatures (3: Khalni), then the reveal engine (4). Within a class: tapped first, newest first. | `SpellEffects.h:8027-8037`, loop `8796-8810` | **Pruning** (one fixed victim order; the search never branches victims) | Misses destroying a colourless / utility land (Radiant Fountain, a spent Fengraf, Myriad) or the red source before a Forest. The rule was written for Stompy ("Forests are the fungible resource"); for Landfall a Forest is a basic that Phylath counts. Victims are now surfaced to the human (4e117469); the SEARCH keeps this rule. |
| 3.6 | **`LandSearchPickOrder` (base)**. ORDER ONLY: the search branches every pick. Rank 0 = picks covering colours we NEED (hand pips) and LACK (board), then more lands, then the generator's **alphabetical** class order. | `DecisionProviders.cpp:1540-1574`; generator `LandSearch.h:126-225` | Order only (default for d0, beyond horizon, human preselection) | Inside the window: nothing (no width). Outside it, **Titan's rank-0 pick is alphabetical**: "Evolving Wilds + Evolving Wilds/Terramorphic" when no colour is lacking, and "Evolving Wilds + Gruul Turf" when red is lacking. That pick drives every d0 game and, with `search_leaf_depth 0`, every Titan in every leaf playout. It is not a reasoned default. |
| 3.7 | **`LandfallCounterTargetOrder` (base)** for Phylath: a body that can attack NOW first, then the highest effective power. Ties go to the oldest. | `DecisionProviders.cpp:1462-1485` | Order only; the engine adds a width cap (1b.3) | The default concentrates counters. The search explores ranks 1-3 as a whole-turn policy. |
| 3.8 | **Karoo bounce: `BounceLandCandidates` + `BounceUserRulePick` + `BounceSearchCandidates` (base)**, all levers default ON (`MTG_BOUNCE_SEARCH`, `_USER_RULE`, `_RULE_AMEND`, `_UNTAPPED_FIRST`). Never another Karoo or an enchanted land. The user rule returns ONE land without search when a single name qualifies. Tapped "clean" lands dominate untapped ones. With no land in hand, all tapped clean lands fold to one (the "replay fold"). At most **3** candidates. | `DecisionProviders.cpp:1933-2056`, `2066-…`, `2265-2395` (width `:2388`); `EngineFlags.h:178,219,232,270` | **Pruning** (USER-ruled 2026-10-08/09) | The dominance argument is in MANA / tempo terms. In a landfall deck, the identity of the returned land also matters on replay (another landfall; a fetch land replays as two). The common Forest-vs-tapped-Evolving-Wilds case nets equal landfalls over the next turn, so I found no concrete loss. UNVERIFIED that none exists. The searched bounce fans only for a PLAYED Turf; a PUT Turf takes the front (2.12): **CLOSED for the put case (8af164b6, merged)**. |
| 3.9 | **`TopLandSourceFirstOnTie` (base, default true)**. On an EXACT tie, the Courser top land sorts before the same-name hand land. | `DecisionProvider.h:1414-1426` | Order only (exact ties) | Ledger Courser Q3, default YES. A top play keeps the hand copy. |
| 3.10 | **`LegendKeepIndex` (base)**: keep the OLDEST copy (Phylath ×4, Omnath ×1). | `DecisionProvider.h:2591-2595`; `SpellEffects.h:17233-17260` | Correctness shortcut | The oldest is the un-sick body. The ETB is banked before the SBA. The binned copy's death still feeds Omnath. Not surfaced to the human (4.8). |
| 3.11 | **`OfferDuplicateLegendCast` (base, `MTG_PRUNE_DUP_LEGEND`)** | `DecisionProviders.cpp:742-806` | Pruning, **inert here** | Prunes only `vanilla_creature` / `lord_effect` legends. Phylath and Omnath are `custom`, so they are always offered. |
| 3.12 | **Cleanup discard: `CleanupDiscardCandidates` = base `CleanupDiscardRanking`**, with `DiscardLandsFirst` false (generic: highest MV, not lands). At a REAL cleanup at depth > 0, every candidate in the hand is trial-rolled and the earliest win kept, with the heuristic breaking ties. Inside rollouts, the heuristic alone decides. | `DecisionProviders.cpp:564-567`, `808-812`; `SpellEffects.h:589`; `AIEngine.cpp:8094-8135` | Searched at the real decision; heuristic in rollouts | Stage 4: 0.0225 sheds/game, every observed shed a tie (`STATUS_QUO_OK`). The Stage 5i bucket policy is AUTHORED, NOT ADOPTED (user decision). |
| 3.13 | **`ShouldAttackWith` (Generic) = attack with everything** | `DecisionProviders.cpp:2466-2469` | Standing attack exemption | No blockers. 0/1 Plants attack for 0 harmlessly. Titan always attacks, so its trigger always fires. |
| 3.14 | **`ManaSourceRank` (Generic) tap order** | `DecisionProviders.cpp:3330` | Standing mana-payment exemption | Picks WHICH legal payment, never whether one exists. |
| 3.15 | **Unpruned A/B hatches that apply**: `terak` (3.4), `xspell` (3.2), `KarooBounce` (3.8), `SearchOrder` (3.1). | `DecisionUnpruned(UnprunedGate::…)` at the cited sites | Measurement hatches | None measured for this deck yet. |

---

### Source 4 — Play-viewer auto-resolved decisions

The deck is **not** "viewer-ready". These choices are resolved without asking the human:

| # | Card / choice | How it is resolved | Source | Classification | Why inert / follow-up |
|---|---|---|---|---|---|
| 4.1 | **Terastodon: WHICH permanents to destroy** | K is offered (the 0..cap fan). The victims are then picked by 3.5 (Forests first), silently. | `SpellEffects.h:8790-8810`; `audit_viewer_decisions.py:361` (classifies it as a `main_phase` K fan only) | **NOT inert** (sweep gi12: Myriad / Mountain / Turf were legal and not fungible with a Forest) | **FIXED (4e117469)**: a `target` board pick per victim, default = the user's order. |
| 4.2 | **Omnath: "any target"** | Always the opponent's face | `SpellEffects.h:9649-9660`; `audit_viewer_decisions.py:1012` ("inert") | Inert by dominance — **PROVISIONAL** | 2.13. |
| 4.3 | **Avenger: "you may" counters** | Always YES, human too | `audit_viewer_decisions.py:591` | Inert by dominance — **PROVISIONAL** | 2.1. |
| 4.4 | **Khalni: "you may" quest counter** | Always YES, human too | `audit_viewer_decisions.py:605` | Inert by dominance — **PROVISIONAL** | 2.2. |
| 4.5 | **Search for Tomorrow: "you may cast it" off suspend** | Always cast, human too | card note; `audit_viewer_decisions.py:1292` | Inert by dominance — **PROVISIONAL** | 2.5. |
| 4.6 | **Haunted Fengraf: which creature returns** | Seeded random (`gyreturn=@random`) | `tools/play/DECISIONS.md:252`; `audit_viewer_decisions.py:604` | Correct by rule (not a player choice) | The randomness model itself is 2.10 (PROVISIONAL). |
| 4.7 | **Instant-speed windows** (Evolving Wilds, Terramorphic, Myriad, Fengraf; Khalni outside the post-Titan combat window) | The human gets main-phase activations only (`landsearch=` / `gyreturn=` verbs), with no combat / end-step / opponent-turn window. The Khalni combat window IS surfaced (`dig`). | `tools/play/DECISIONS.md:251-252` | Inert vs a passive opponent — **PROVISIONAL** | 2.9 / 2.3. |
| 4.8 | **Legend rule: which Phylath survives** | Keep oldest; no chooser | `SpellEffects.h:17233-17260` (no human chooser found) | Near-inert (only sickness differs; Phylath's value is ETB + landfall) | Disclose; no wiring proposed. |
| 4.9 | **Order of simultaneous triggers** (Genesis Wave pass 2; several landfall watchers on one land) | Fixed dominant CR 603.3b order / fixed dispatch order | `landfall-trigger-infrastructure.md` §1; `LandSearch.h:48-54` | Inert (commuting payloads; stated dominance) | 2.16(c), 2.22. |
| 4.10 | **Khalni in-combat crack prompt default** | Default 0 "crack now", labelled as the AI's pick while the AI held (cosmetic) | `LandSearch.h:836-843` (prompt at `:842`, default arg `0`) | Cosmetic mislabel | **FIXED (499c7348)**: default keep, crack wording. |
| 4.11 | **Positioned-land menu entries with an unpayable prefix**, and k == -1 twins | Offered to the human; picking one silently drops a cast (sweep flags 1-2, 13/16 and 7/16 games) | `TurnSolver.cpp:54573` ("does less, scored as one") | Illegal / duplicate menu entries (and wasted search) | **FIXED (d4e6a424, a1ffac94)**: prune plus post-apply fold; the human menu hides the twin. |
| 4.12 | **Terastodon plan summary omits K** | Cosmetic | sweep gi5 | Cosmetic | **FIXED (28a0dfc2)**. |

**Surfaced, NOT gaps** (for completeness): every land-search pick (`tutor_etb`, sequential per land,
-1 = stop / decline for Titan, Cultivate, Rampant Growth, Myriad); Cultivate's split (prompt order);
Phylath's target (`target`, over every Plant); land position (`landafter=` / `landat=`); Courser top
land (`landtop=`); suspend (`suspend=`); Genesis Wave X (every legal X) and its put subset (`dragon`,
preselected); every Karoo bounce, played OR put (`bounce`); the Terastodon K fan.

### Flag for the user (surprising items)

1. **Genesis Wave is max-X only (3.2).** "Cast Phylath / Avenger, then a smaller Wave" in one main
   phase is never enumerated, at any budget. In this deck that may be the strongest Wave line.
   `MTG_UNPRUNED=xspell` is the measurement.
2. **No deck cast order; the generic rank puts every creature before every ramp spell (3.1).**
   "Rampant Growth / Cultivate / SfT, then Avenger / Phylath (count the fetched land)" is
   inexpressible in one main phase. Activations got position twins; spells did not. Cast order is
   yours to author.
3. **The analyzer's static keep rule (1.9).** `two_drop` requires Rampant Growth or Khalni (8 cards;
   SfT's {G} suspend does not count). It mulligans about 35% of 7-card and 41% of 6-card hands on
   that clause alone -- and it MEASURES better than no curve check (+0.218 turns for `none`). Mulligan policy is yours.
4. **Titan's default (rank-0) land pick is alphabetical (3.6).** Two Evolving Wilds-class lands, or
   Evolving Wilds + Gruul Turf when red is lacking. It decides every d0 game and, under
   `search_leaf_depth 0`, every Titan in every leaf playout.
5. **Representation caps that hold at unlimited budget (1b.1-1b.3).** Land-search product ≤64 then
   one-hot, one axis at a time, Phylath target ≤4 classes as a whole-turn policy. Each is a question
   for the completeness-at-large-budget invariant.
6. **Terastodon (3.4 / 3.5 / 4.1)**: K ignores next-turn landfall growth, and the Stompy-derived
   Forests-first victim order eats the basics Phylath counts before colourless utility lands (Review
   Q2, still open).
7. Stale bracket text (Genesis Wave, Terastodon, all four Karoos) -- FIXED in 6d08ec65 (text only).
8. **Karoo bounce rules reason in mana terms only (3.8).** I found no concrete loss for landfall
   (replay-landfall counts net out in the common case), but the identity of the returned land was
   never argued for a landfall deck.

## Open questions for the user (collected; work continues on the stated default)
- **(Courser integrator Q1 — CAST ORDER, USER-OWNED)** In a positioned-land plan, landfall-watcher casts
  go before other spells of the SAME cast-order rank. Only the arbitrary hand-order tie-break moves;
  no rank changes, and the canonical plans are untouched. This is what makes "Khalni, land, Rampant
  Growth" reachable whichever card the hand lists first. Taken as the default; the user decides.
- (Courser integrator) `landat=<k>` requires cast k to be a landfall card; widening it for the viewer
  is the user's call.
- (Land-search Q1) Second main for Titan's attack trigger: default NOT enabled; build the in-combat
  Khalni pin instead.
- (Land-search Q2) Dominance collapses (Titan "you may search", "up to two" on Cultivate/Myriad/Khalni/
  Titan, Rampant Growth fail-to-find, Search for Tomorrow's "you may cast" off suspend): collapsed for
  the search, the human keeps the decline — PROVISIONAL.
- (Land-search Q3) Titan land-pair narrowing (~25 pairs): default FULL identity-folded list; the
  proposed ranking is not adopted until a 5e A/B.
- (Land-search Q4) Fix Creature Giving's dead Crop Rotation land-target axis (in-window greedy) with its
  own rebaseline — default: separate item, surfaced.
- (Review Q2) Terastodon's K rule in a landfall deck: give the deck its own logic (count next-turn
  landfall growth; destroy colourless lands before Forests)? Default: keep the user's rule, measure
  unpruned in 5e, report.
- (Review Q3) Genesis Wave puts a duplicate legend (weakly dominant) — default YES (implemented).
- (Review Q4) Haunted Fengraf's "at random" is a seeded value the clairvoyant search reads, plus the
  instant-speed collapse to end of main — default YES (PROVISIONAL).
- (Courser Q2) Search may use the known post-shuffle order to choose "shuffle, then play the revealed
  top land" — default YES (same clairvoyance as fetch-then-draw; disclosed).
- (Courser Q3) Exact tie: order top-land before same-name hand land (ordering only) — default YES.
- (Courser Q4) Titan attack-trigger shuffle can expose a playable top land after combat (a second-main
  case). Researcher proposed a restricted main 2 behind a default-off lever; CLAUDE.md's "delete,
  don't lever" applies, so the default is to measure the capability and report, not ship it gated.
- (Courser Q5) Saved log records top plays with their source plus a per-phase top snapshot — default YES.
- (Runaway, perf proposal 1) Melira's mulligan trial settings for Landfall (`bottom_eval_depth: 0`,
  `bottom_eval_topk: 5`): 0.56-0.69x units on the four searched regression cases; quality UNRESOLVED
  (1 better / 2 worse over 150 paired games, net +1 turn, far too small; Melira's adoption used
  2,000-3,000 paired games). Trial games are still ~99% of gi38's remaining cost. NOT adopted —
  mulligan policy is the user's; default: unchanged.
- (Runaway, perf proposal 2) A last-turn relevance collapse of land-search / attack / crack pins at the
  horizon's final turn: needs a per-state soundness proof (not an identity fold), so NOT built.
- (Gaps) Human play gets no Search for Tomorrow credit (the human picks the basic and is re-prompted) —
  kept. Crop Rotation does not share the mechanism (Creature Giving unchanged) — separate item with its
  own rebaseline. The remaining gap-3 cost (+10% wall, up to +5% units over gap A alone; mostly breakpoint
  continuations of pending Titan-attack variants) — the per-continuation fold is DEFERRED in
  `land-search-cluster.md`; building it is the user's call. Known representation limits listed there
  (one bounce sub-choice per pick, one Wave bounce pin per plan, the 6-bit packed-pin cap, sizing off the
  library at enumeration, two searches racing for the last basic).
- (Sweep fixes) A Genesis Wave / Natural Order PUT Terastodon: the human picks the victims but not K (K is
  the provider's projection) — default kept, deferred item 8. The human's K menu still caps at the count
  of OWN noncreature permanents although victims may now be the opponent's — unchanged. Unpayable
  positions inside breakpoint continuations are still enumerated (wasted, not wrong) — deferred item 7.
  Unchecked: a menu label "(after <card>)" may name the wrong watcher when cast-order rank reorders casts
  (human GUI lines pin the clicked order, so they are unaffected).
- (Runaway) SolveWithLookahead's start-gate estimate was ~7,300x low on the gi38 root; the estimator's
  calibration is left alone (the ceiling now caps the damage at 25x).
