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

## Stage 5j — ADDED to the suite: cost gate PASS 2.64x (2026-10-16 ~09:30, rebased onto origin 5f1db12ab)
USER L1: *"let's add it with a slightly smaller set until the keep table is done."* Rows as proposed below,
resized: smoke d0 1000 / d3 30 b10 / d5 15 b20; regression d0 1000, d3 2x40 b10, d5 2x20 b20 (s2002, s3003);
overnight d0 4x2000, d3 4x250 b10, d5 4x150 b20 (s4004-s7007). Expand at the keep-table adoption
(`regression-testing.md`, "Sizing a deck's overnight rows").
- **Isolation, twice:** with the fetch rule (9d191ca1) on the pre-rebase tree, and again on the tree rebased
  onto origin 5f1db12ab (272 commits), full smoke and full regression left every other case unchanged
  (smoke 119 / regression 166 `configs changed: 0`, slower=faster=0); only the 8 Landfall keys are NEW.
  Unit tests 1261/1261 on the rebased tree.
- **Cost gate, re-measured from that one pooled regression run** (`suite_gate.py --measure-all --no-run`,
  then `--cost decks/Landfall --no-run`): **landfall 6,899 ms/game (d3 s2002) / pd 2,611 = 2.64x, limit 3x
  -> PASS.** Per searched case: d3 s2002 6,899; d5 s2002 5,802; d5 s3003 5,345; d3 s3003 2,653.
  **NOT all of the drop from 4.30x is the prune.** The rows keep the same seeds with fewer games, and the
  resize of d5 s2002 from 25 to 20 games drops gi20 (s2022) and gi22 (s2024) -- two of the three heaviest d5
  games in the census above. The prune is 0.69-0.71x units on held-out play. The cost at the ORIGINAL
  50/25 shape with the prune, pooled with the whole regression tier on the same binary copy
  (`logs/lf_cost_fullshape/`): **landfall 9,234 ms/game (d5 s2002, 25 games) / pd 2,976 = 3.10x** -- d5
  s2002's games 20-24 average ~23 s/game against ~5.8 s for its first 20. So the prune took that shape from
  4.30x to ~3.1x, AT the line rather than under it (PD's own worst cell moved 2,611 -> 2,976 between the two
  runs, ~14%), and the 2.64x PASS rests on the user's smaller rows. The user chose those rows knowing the
  deck was over (L1), so this is reported, not a blocker. The bottoming trial games are still the bulk, and the keep
  table still replaces them.
- **Overnight rows** (`--overnight --deck=landfall`, alone on the box, PGO): 12 keys, makespan 5m9s, 7,105
  core-s (d5 4x150 3,656; d3 4x250 3,448; d0 4x2000 0.6). d0 7.636-7.699, d3 5.816-5.988, d5 5.707-5.880.
- All 20 keys accepted with `--deck=landfall` per tier; `check_gt_logs.py` 706 consistent, 0 stale.

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

## USER fetch rule — colour sequence + prune (2026-10-16, 9d191ca1)
The user asked to "double check to make sure that land searches are reasonable" and then specified the
rule over several messages (verbatim in `LandfallProvider`'s .cpp block): every BASIC search takes the
sequence green, green, red, green, red (g = our green-making permanents; r = red-making permanents plus
Mountains in HAND -- a Gruul Turf in hand is NOT counted, it enters tapped); Myriad Landscape takes the
Mountain pair when >= 2 remain and g >= 2, else Forests; when an on-board Myriad is the only search left
(and >= 2 Mountains remain) the others take green so Myriad "fetches the reds together"; and "the
important thing here is to make the searches cheap here by pruning": each basic search is PRUNED to the
rule's one pick (new hook `LandSearchPickWidth`; human play unpruned; `MTG_UNPRUNE=landpick` lifts it).
Primeval Titan's any-land pick is untouched and still searched.
- The literal first version (Mountains first, no green guard) measured **+0.044 turns at d0** (se 0.010)
  -- green starvation: a T2 Mountain beside a colourless land stranded Rampant Growth / Avenger (gi66,
  gi115). The user then amended to green first.
- **Final, paired vs b35f9f0e (held-out):** d0 4,000 games -0.011 (se 0.007); d3 b10 500 games **-0.030**
  (se 0.011, 24 better / 9 worse), 0.71x units; PLAY d5 b20 1,000 games **-0.051** (se 0.010, 75/24),
  0.69x units. Order-only (prune lifted) +0.002 (neutral): **the prune itself is the gain** (-0.052 vs
  order-only) -- the freed budget goes to better lines.
- 6a: a new Source 3 row -- `LandfallProvider::LandSearchPickOrder` + `LandSearchPickWidth`: **Pruning,
  USER-owned rule** (a basic search can no longer deviate from the sequence's pick at any budget; the
  human keeps every pick).

## USER Titan pick + Karoo bounce rules — both pruned (2026-10-16, 73e30dd3f)
Asked whether Primeval Titan's pick should get a rule too, the user answered "Primeval Titan should be
pruned" and gave it (verbatim in `LandfallProvider`'s .cpp block), then the bounce rule in a follow-up:
- **Titan** (`LandfallTitanAllowed`): "you usually want one of three" -- Evolving Wilds / Terramorphic
  (one identity class already), Gruul Turf, Myriad Landscape. Turf is automatic with **no land card in
  hand**, or when short of a colour (**g < 3 or r <= 1**, the fetch rule's counts), beside Wilds or Myriad;
  otherwise the Wilds / Myriad pairs, **searched** among those ("We might be able to get away with
  searching this"). With **no basic left** in the library: Turf, Myriad, Radiant Fountain, Haunted
  Fengraf, Wilds -- one pick. A slot those lands cannot fill stays searched; with no Turf wanted and no
  Wilds / Myriad left, Titan is not narrowed.
- **Karoo bounce** (`LandfallProvider::BounceUserRulePick`, every Turf bounce): "bounce in reverse order
  from colour requirements ... Colourless producing lands can go first, then extra red or green." Among
  lands that re-enter untapped (tapped, or floatable): colourless first, then the most SPARE colour (our
  lands making it, less the most pips of it on one of our nonland cards -- G 3 Genesis Wave, R 2 Omnath,
  the targets the fetch rule builds toward), then tapped, then copies. One land, no search.
- **Interpretations taken (reported to the user):** "no land drop" = no land card in hand; the colour
  counts are the fetch rule's (a Mountain in hand is red); a colour's "requirement" is the deck's own
  most-pips card.
- **Held-out A/B vs d380133bf** (fresh seeds, paired, unwon = 9; `logs/titanrule/`):

  | setting | turns | better / worse | units | wall |
  |---|---|---|---|---|
  | d0, 4,000 games | **-0.014** (se 0.005) | 143 / 127 | -- | -- |
  | d3 b10, 500 | 0.000 (se 0.008) | 6 / 7 | **0.33x** | 0.52x |
  | PLAY d5 b20, 1,000 | **-0.005** (se 0.003) | 6 / 1 | **0.36x** | 0.41x |

  The one slower play game (s1400000 gi39, T6 -> T7) shares the kept 7 with the base. At turn 1 the
  search sees no win in its horizon and breaks the tie by skipping the land drop (then discards Titan at
  cleanup). It wins on T6 again at 2x budget, so the line is still findable. With the bounce rule off on
  both binaries it is T6 on both, so the Titan rule is clean there.
- **Tiers** (full smoke + full regression, 73e30dd3f): every other case unchanged (smoke 119 / regression
  166); Landfall's 8 keys moved -- searched cases 0 slower in both tiers (1 faster), d0 churn 35/39 and
  39/28 slower/faster (regression d0 7.6300 -> 7.6290). Accepted with `--deck=landfall`; `check_gt_logs`
  706 consistent. Overnight left to the periodic run (a play change).
- **Cost gate, same regression run:** landfall **2,881 ms/game (d5 s3003) / pd 2,695 = 1.07x** (was 2.64x
  at these rows this morning, 4.30x before the fetch rule); the deck's regression total 605 -> 220 core-s.

## Stage 5c2 — horizon-honest tie-break: NO SIGN at this sample (2026-10-16, tree b35f9f0e)
`leaf_tiebreak_check.py`'s manifest (4 blocks x 250 at PLAY settings d5 b20, both arms per-job, pooled
with the bottoming A/B): **2 changed games of 1,000 paired (0.2%), both BETTER with the tie-break (net
-2), 0 worse.** Below the 20-change floor, so no direction is claimed; the script asks for ~21 blocks
(~5,250 paired games at d5). Landfall wins well inside the horizon (T5-T6 at d5), so the leaf rarely
binds. Default ON stays (a lever that does not fire costs nothing). The decisive run is DEFERRED to after
the keep table, when the bottoming trial games (92-99% of today's cost) are a table read and the sample
is ~20x cheaper -- recorded, not skipped.

## Stage 5e — oracle diffs vs the shipped narrowings (2026-10-16, tree b35f9f0e)
Paired per game against the shipped arm of the 5c2 batch (same seeds 1.0M/1.1M x 250, PLAY settings d5
b20); each oracle is process-wide, so its own batch.
| oracle arm | turns vs shipped | worse / better | units |
|---|---|---|---|
| `MTG_UNPRUNE=xspell,terak` (Genesis Wave full X range + Terastodon K fan 0..cap) | -0.002 (se 0.002) | 0 / 1 | 1.06-1.08x |
| `MTG_SEARCH_ORDER=1` (cast-ordering search) | +0.002 (se 0.002) | 1 / 0 | 1.09x |
- The one better game (s1100151 gi151, 7 -> 6) is `xspell` alone (`terak` alone = 7). It diverges on T2
  (Gruul Turf vs a Terramorphic crack) and the T6 win NEVER casts Genesis Wave: the wider X menu changed
  the search's valuation of the rollouts, not the realised Wave. The SHIPPED search at b200 also wins T6
  -> budget starvation at b20, not a line the max-X prune makes unfindable.
- The one worse game (s1100228 gi228, 5 -> 6 under the ordering search) recovers to T5 at b200: the
  permutation search diluting a fixed budget (the 5e step-4 caveat).
- Verdict: neither the max-X rule, the user's Terastodon K rule nor the generic cast order shows a loss
  on 500 games. These remain disclosed narrowings (6a 3.1 / 3.2 / 3.4); the user's own calls (cast order,
  Terastodon K) are unchanged and still surfaced.

## verify_deck.py (2026-10-16 ~06:00, tree e05b38c2) — the generated block at the end of this file
PASS: coverage (20/20 full), viewer (self-guard + surface sweep), mismatch (0 nonconv / 0 fd-diverge,
seeds 7001/7002 x 60, both arms), play_invariants (8 games / 436 decisions), claude_sweep (0 unresolved).
FAIL, each explained:
- `regression_tiers` / `suite` -- the 5j cost FAIL above (USER decision; rows not added).
- `card_costs` -- Scryfall HTTP 429 after the first ~300 cards: a DID-NOT-RUN on 120 OTHER decks' cards.
  Landfall's 20 were hand-verified against live Scryfall in 2d-bis (all match).
- `card_fields` -- Saproling Migration (kicker) and Kili (storied) keywords: pre-existing, not this deck's
  cards; need keyword-enum support (open question, surfaced since 10-15).
- `viewer_wiring: tutor_etb` -- FIXED after the run: the type was emitted (`WriteTutorDecisionJson`) and
  rendered (`tutorPanelHtml`) but had no row in the `tools/play/DECISIONS.md` registry; row added, the
  static re-run passes (bounce, dragon, target, tutor_etb wired).

## Stage 5g — earliest-win miner: GAUGED, full run DEFERRED (2026-10-16, tree e05b38c2)
`GAMES=10 SEED=2002 scripts/mine_heuristics.sh` at the skill's defaults (d5 b3000, MTG_SEARCH_ORDER=1,
every turn), with a MINING-ONLY profile copy setting `bottom_eval_depth 0` (the trial games at b3000
would dwarf the mining; it shifts only WHICH hands get mined, never how a decision is scored): **76 min
wall for 10 games / 58 decisions** (tail-bound by one game). Earliest-win ties are WIDE (median 6
candidates per decision, max 546 -- the land-search pin space), so ORDER rules need a large sample: the
skill's minimum (2 seeds x several hundred games) extrapolates to well over 10 h. Preliminary signals at
n=58 (NOT trusted, no rule proposed): ORDER -- none with support; INCLUSION -- Khalni -0.60 and Phylath
-0.58 (cast them), Terastodon +1.00 early (0 help / 3 hurt; leave to the search), Avenger +0.40
situational; LAND -- Forest dominates the earliest-win lines, then Gruul Turf, Radiant Fountain, Myriad.
The 5e cast-order oracle already found nothing over the generic order on 500 games. DEFERRED to after
the value leaf (the O(1) leaf removes most of each candidate's rollout cost) -- recorded, not skipped.

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

**UPDATE 2026-10-16: `LandfallProvider` now carries three USER rules** -- the basic-search colour sequence (pruned to one pick), Primeval Titan's pick (pruned to the Turf / Wilds / Myriad picks) and the Karoo bounce (one land, no search); rows 3.6 and 3.8. The rest of this section was written when it **overrode NOTHING.** It was an EMPTY `DeckProvider` derivation: only `Certificate()`
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
| 3.6 | **`LandSearchPickOrder` (base)**. ORDER ONLY: the search branches every pick. Rank 0 = picks covering colours we NEED (hand pips) and LACK (board), then more lands, then the generator's **alphabetical** class order. | `DecisionProviders.cpp:1540-1574`; generator `LandSearch.h:126-225` | Order only (default for d0, beyond horizon, human preselection). **SUPERSEDED 2026-10-16 by the USER's rules in `LandfallProvider`: basic searches take the colour sequence and are PRUNED to that pick; Titan is PRUNED to Turf + Wilds/Myriad (no land in hand, g < 3 or r <= 1), else the Wilds/Myriad pairs, and with no basic left to one fixed-order pick -- `MTG_UNPRUNE=landpick` lifts both** | Inside the window: nothing (no width). Outside it, **Titan's rank-0 pick is alphabetical**: "Evolving Wilds + Evolving Wilds/Terramorphic" when no colour is lacking, and "Evolving Wilds + Gruul Turf" when red is lacking. That pick drives every d0 game and, with `search_leaf_depth 0`, every Titan in every leaf playout. It is not a reasoned default. |
| 3.7 | **`LandfallCounterTargetOrder` (base)** for Phylath: a body that can attack NOW first, then the highest effective power. Ties go to the oldest. | `DecisionProviders.cpp:1462-1485` | Order only; the engine adds a width cap (1b.3) | The default concentrates counters. The search explores ranks 1-3 as a whole-turn policy. |
| 3.8 | **Karoo bounce: `BounceLandCandidates` + `BounceUserRulePick` + `BounceSearchCandidates` (base)**, all levers default ON (`MTG_BOUNCE_SEARCH`, `_USER_RULE`, `_RULE_AMEND`, `_UNTAPPED_FIRST`). Never another Karoo or an enchanted land. The user rule returns ONE land without search when a single name qualifies. Tapped "clean" lands dominate untapped ones. With no land in hand, all tapped clean lands fold to one (the "replay fold"). At most **3** candidates. | `DecisionProviders.cpp:1933-2056`, `2066-…`, `2265-2395` (width `:2388`); `EngineFlags.h:178,219,232,270` | **Pruning** (USER-ruled 2026-10-08/09). **2026-10-16: the USER's Landfall rule** (`LandfallProvider::BounceUserRulePick`) replaces the base pick for every Turf bounce: among lands that re-enter untapped (tapped, or floatable), colourless first, then the most SPARE colour (our lands making it less the most pips of it on one of our nonland cards: G 3, R 2), then tapped, then copies -- ONE land, no search (`MTG_UNPRUNE=karoobounce` reopens it) | The dominance argument is in MANA / tempo terms. In a landfall deck, the identity of the returned land also matters on replay (another landfall; a fetch land replays as two). The common Forest-vs-tapped-Evolving-Wilds case nets equal landfalls over the next turn, so I found no concrete loss. UNVERIFIED that none exists. The searched bounce fans only for a PLAYED Turf; a PUT Turf takes the front (2.12): **CLOSED for the put case (8af164b6, merged)**. |
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

## Decisions awaiting the user — CONSOLIDATED (2026-10-16 ~07:15; default taken in brackets)
| # | decision | default taken | recommendation |
|---|---|---|---|
| L1 | **ANSWERED 2026-10-16 (USER): add Landfall to the suite now with a slightly smaller set until the keep table is done** (smoke d3 30 / d5 15; regression d3 2x40 / d5 2x20; overnight d3 4x250 / d5 4x150). Original question: **5j route.** Landfall is 4.30x PD in one pooled run; 92-99% of the cost is mulligan-bottoming trial games, which the keep table replaces. (1) lift the gate for Landfall (`MTG_ALLOW_UNTESTED_DECK=1`, user-only) to build the keep table + value leaf; (2) rule that 5j measures post-artifact cost; (3) cheaper bottoming | [DONE 2026-10-16: added, 20 keys in all three tiers on the tree rebased onto origin 5f1db12ab; gate 2.64x at these rows, ~3.1x at the first-proposed 50/25 -- see 5j] | **(2) via (1)**, as recommended for PD. Not (3): Melira shape +0.036 turns (0.59x units), all-greedy +0.126 (0.19x) |
| L2 | `search_leaf_depth: 0` in the profile (greedy horizon leaf, Melira precedent) | [ADOPTED 223cbba4: -0.141 turns, se 0.031, 62/23, 0.47-0.65x units] | Confirm |
| L3 | Mulligan keep rule `curve_check: two_drop` (only RG/Khalni qualify; ~35% of 7s fail) | [kept] | Keep: `none` measured +0.218 turns worse (se 0.035) |
| L4 | Cast order (user-owned): no Landfall order authored; generic = creatures before ramp. Also the Courser Q1 tie-break (watchers first within a rank) | [generic + Q1 tie-break] | Generic looks right for landfall payoffs (a later land pumps / adds tokens); the 5e ordering oracle found nothing better over 500 games |
| L5 | Terastodon K rule + Forests-first victims in a landfall deck (Review Q2) | [user's rule kept for the search; victims now surfaced to the human] | Keep for now: `MTG_UNPRUNE=terak` found no loss over 500 games |
| L6 | Discard BUCKET policy (Stage 5i, authored) | [NOT adopted] | Review; sheds are rare (0.0225/game, all ties) |
| L7 | New default-ON levers: `MTG_LA_PASS_CEILING` (budget re-price, inert unlimited), `MTG_PUT_BOUNCE_FOLD` (exact fold) | [ON] | Keep ON (both re-price or fold only) |
| L8 | Representation caps that bind at ANY budget (6a 1b.1-1b.3: land-search product <=64 then one-hot, one axis per variant, Phylath target <=4 classes) -- a completeness-invariant question | [unchanged] | Schedule as engine items if you want them findable at large budget |
| L9 | Titan's rank-0 land pick is alphabetical (drives d0 and every leaf playout under leaf depth 0) | [unchanged] | A reasoned provider default would need a 5e A/B; worth one |
| L10 | Provisional collapses / deferrals: Avenger + Khalni "may", Omnath face, every "up to" collapse, SfT "may cast", instant-window collapses, Fengraf seeded random, Genesis Wave dup legend, Courser Q2/Q3/Q5 | [PROVISIONAL YES] | Approve (each argued in 6a Source 2) |
| L11 | Courser Q4 / Land-search Q1: no autonomous main 2 for the Titan-attack top land | [not built] | Measure the capability before deciding |
| L12 | Crop Rotation (Creature Giving) on the shared land-search path | [separate item] | Do it with its own rebaseline |
| L13 | Kili (storied) / Saproling Migration (kicker) keyword-enum gaps (other decks) | [surfaced] | Small data/enum item |
| L14 | `landat=<k>` only for landfall-watcher casts in the viewer; PUT Terastodon K not asked of the human; K menu caps at own permanents | [unchanged] | Low priority |
| L15 | Deferred measurements: 5c2 decisive tie-break sample, 5g miner at full size | [after the keep table / value leaf] | -- |

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

<!-- verify_deck:begin (generated -- do not edit inside) -->
## Last verification (2026-10-16)

`verify_deck.py decks/Landfall/Landfall.cod --write-ledger` -> **FAIL**

| Gate | Status | Blocking | Summary |
|---|---|---|---|
| coverage | PASS | yes | all 20 cards full (missing=0, partial=0) |
| card_costs | FAIL | yes | cost audit INCOMPLETE -- 120 cost(s) UNVERIFIED (Scryfall rate-limited/unreachable) |
| card_fields | FAIL | yes | 2 Scryfall field mismatch(es) |
| clause_ledger | SKIP | no | covered by coverage+bracket-notes+oracle-diff |
| regression_tiers | FAIL | yes | not in the regression suite (no tier) |
| viewer | PASS | yes | self-guard + surface sweep clean |
| viewer_wiring | FAIL | yes | 1 wiring gap(s) |
| mismatch | PASS | yes | no nonconv/fd-diverge across seeds [7001, 7002] x 60 games (both arms completed) |
| play_invariants | PASS | yes | 8 game(s)/436 decisions: determinism+integrity+progress hold |
| claude_sweep | PASS | yes | Claude-play sweep recorded, 0 unresolved flags |
| suite | FAIL | yes | Landfall is NOT in test/regression_cases.sh |

### Pending user sign-off (block the gate until fixed OR approved below)
Add a key to `## Approved deferrals` to sign one off (only if it is a genuine, understood deferral -- not a bug):
- `card_costs:*` -- 120 card(s) could not be fetched, so their costs were never compared -- a did-not-run, not a mismatch. Re-run when the rate limit clears.
- `card_costs:Hatchery Sliver` -- Hatchery Sliver: HTTP 429
- `card_costs:Aether Vial` -- Aether Vial: HTTP 429
- `card_costs:Thrumming Hivepool` -- Thrumming Hivepool: HTTP 429
- `card_costs:Treasure Hunt` -- Treasure Hunt: HTTP 429
- `card_costs:Land's Edge` -- Land's Edge: HTTP 429
- `card_costs:Throes of Chaos` -- Throes of Chaos: HTTP 429
- `card_costs:Dauntless Bodyguard` -- Dauntless Bodyguard: HTTP 429
- `card_costs:Venerable Knight` -- Venerable Knight: HTTP 429
- `card_costs:Swords to Plowshares` -- Swords to Plowshares: HTTP 429
- `card_costs:Invigorate` -- Invigorate: HTTP 429
- `card_fields:Saproling Migration` -- Saproling Migration: keywords local=[] scryfall=['kicker']
- `card_fields:Kíli the Resourceful` -- Kíli the Resourceful: keywords local=[] scryfall=['storied']
- `regression_tiers:too_slow` -- absent from all three tiers -- add it to ALL of them (analyze-deck 4-bis), or record the measured cost and get the user's sign-off while it does not fit
- `viewer_wiring:tutor_etb` -- type 'tutor_etb' not in DECISIONS.md registry (unmapped)
- `suite:not_a_case` -- Landfall has no regression cases, so no ground truth tracks its play. The value leaf and mulligan profile are BLOCKED until it is added (3x cost rule: scripts/suite_gate.py --cost). See analyze-deck.md Stage 5j.

### Stage 6a disclosure (deferrals + not-yet-built checks)
- coverage deferral -- Rampaging Baloths: LANDFALL: landfall_creates_tokens 1 + landfall_token_power/toughness 4/4, subtypes [Beast
- coverage deferral -- Avenger of Zendikar: ETB: etb_tokens_per_land 1 + the shared etb_created_token_* spec (0/1, [Plant
- coverage deferral -- Khalni Heart Expedition: LANDFALL: landfall_quest_counters 1 through PutQuestCounters (Doubling Season doubles it), once per land played or put (OnLandEntered). The 'you may' is resolved YES by DOMINANCE: a quest counter is only ever SPENT by the optional activation, so one more never removes an option -- no cap, no threshold that punishes, the passive opponent never interacts -- the Beastmaster Ascension precedent. ACTIVATION: PermAbilityMode::QuestSacLandSearch -- remove three quest counters and sacrifice it (land_search_activation_remove_quest 3 + land_search_activation_sacrifice_self; NO {T}, so it is usable the turn it enters and while tapped), then PerformLandSearch puts up to two basic land cards onto the battlefield tapped (each a landfall event through EnterLand) and shuffles once. A searched optional trailing activation of a main phase (holding it for a landfall payoff is a real line); a mid-plan landfall that brings it to three opens the site-9 breakpoint (its own activation key), so the crack is reachable the same main phase. WHICH basics = the in-plan land-search pin; the human picks each at resolution (`tutor_etb`; board verb landsearch=). 'Up to two' collapsed for the search to as many as the library allows (PROVISIONAL dominance). IN COMBAT: an Expedition filled by Primeval Titan's attack trigger can be cracked in the declare-attackers step, before damage (FireCombatLandSearchCracks; searched by Plan::combat_crack_choice, a bitmask over the Expeditions; the human is asked through the dig prompt), so its two landfalls pump ATTACKING Plants. A plan that also casts an ETB that counts lands gets searched crack POSITIONS (Plan::land_search_act_slot, in Plan::land_slot's encoding: before every cast, or right after the k-th landfall-watcher cast). PROVISIONAL: the windows AFTER combat damage (main 2 / end step -- the passive opponent takes no turn) are collapsed: an Expedition full before combat is cracked in a main phase, and one filled in combat is weakly better cracked in combat (same lands, same untap next turn, plus the combat pump) than at the end step.
- coverage deferral -- Search for Tomorrow: land_search_on_resolve: PerformLandSearch -- one basic land card onto the battlefield UNTAPPED (the basic's own entry) through EnterLand, so landfall fires; one ShuffleAfterSearch. WHICH basic = the in-plan land-search pin (hard cast). SUSPEND: suspend_time_counters 2 + suspend_cost {G} -> an Action::Kind::Suspend plan action (the suspend= viewer verb) sharing the hand slot with the hard cast (mutually exclusive), the {G} paid by the shared ApplySuspend at every apply site (stranded -> no suspend); arrive_turn = turn+2 (suspend T1 -> cast T3 upkeep, BEFORE the draw step, so the arrival's shuffle precedes the draw). The off-suspend arrival is a CAST (storm count) routed through CastOffSuspend's non-permanent branch: it resolves the search and goes to the graveyard; its basic is the searched UPKEEP pin (Plan::upkeep_land_search_choice -> GameState::scripted_upkeep_land_search, the Aether Vial next-upkeep route). 'You may cast it' is collapsed to CAST, for the search and the human alike: declining leaves the card exiled with no time counters and gains nothing; only the shuffle differs, which is neutral in real-information terms (PROVISIONAL). HARD-CAST MANA (land-search gap 2, closed 2026-10-15): the untapped basic funds the same main phase's later casts -- the cast is emitted once per land-put CREDIT class of its picks (a Forest class credits {G}, a Mountain class {R}; Action::rock_mana, LandSearchCreditGroups) and each variant's resolution is restricted to the picks earning its credit, so 'Search for Tomorrow, then Rampant Growth paid with the fetched Forest' is a plan and no colour is promised the chosen basic cannot make. Only all-basic untapped picks are credited; human play carries no credit (the basic is the human's prompt).
- coverage deferral -- Cultivate: land_search_on_resolve + land_search_count 2 + land_search_to_hand 1: PerformLandSearch takes an ORDERED pick (battlefield basic, hand basic) from the provider-ordered LandSearchPicks -- every (battlefield, hand) split of every maximal pair of basics, identity-folded over copies; the battlefield one enters TAPPED through EnterLand (landfall), the hand one through EnterHand(Tutor), which opens the put-in-hand breakpoint (site 10) so the fetched basic can still be the turn's land drop; a lone find goes to the battlefield (ruling 2010-08-15). ONE shuffle. The reveal is public information (logged in the viewer history). 'Up to two' is collapsed for the search to as many as the library allows (dominance: one card removed from an otherwise identical future; the hand basic can at worst be the cleanup discard, which is our choice -- PROVISIONAL); the human picks each land at resolution (two `tutor_etb` prompts) and may stop early.
- coverage deferral -- Primeval Titan: land_search_on_enter_or_attack: ONE printed trigger, two fire sites (the Inferno Titan precedent). ENTERS: FireOwnEtbTriggers (the universal own-ETB cascade, every world, Genesis Wave puts included) -> PerformLandSearch, up to two LAND cards of any kind (ruling 2017-11-17) onto the battlefield TAPPED: both placed through EnterLand (each a landfall event), THEN their own enter triggers (Gruul Turf's mandatory bounce -- which may return a land fetched beside it -- and Radiant Fountain's 2 life), then ONE shuffle. ATTACKS: FireAttackLandSearch at the declare-attackers trigger point in both worlds (declared attackers only, CR 508.4), BEFORE damage, so the fetched lands' landfall pumps reach attacking Plants this combat -- priced by PendingAttackDamage's copy-and-diff addend (CountAttackLandSearchPump); attacker indices are repaired by number after a Karoo bounce. WHICH lands is SEARCHED over the FULL identity-folded pick list (no provider width): the in-plan land-search pin for the ETB, Plan::attack_land_search_choice (GameState::scripted_attack_land_search, packed per attacking Titan) for the attack. 'You may search' and 'up to two' collapsed for the search by dominance (PROVISIONAL); the human may decline (-1 on the first prompt, no shuffle) or stop at one. Trample is inert vs a non-blocking opponent. A fetched Gruul Turf's bounce is SEARCHED (land-search gap 3, closed 2026-10-15): a sub-choice of the pick on the same pin (the extended pick list, LandSearchPinDecode), for the ETB and the attack trigger alike.
- coverage deferral -- Evolving Wilds: A REAL battlefield land with NO mana ability: template custom, so no mana-source site counts it (a basic_land template with empty produces would tap for a phantom {C}). It ENTERS (its own landfall) and '{T}, Sacrifice: search' is a SEARCHED activation (PermAbilityMode::SacLandSearch, armed by land_search_activation_cost '' + land_search_activation_taps + land_search_activation_sacrifice_self): the {T} is paid first, the land is sacrificed as part of the cost (graveyard, the sacrifice watchers -- not a creature dying), then PerformLandSearch puts one basic onto the battlefield TAPPED through EnterLand (a second landfall) and shuffles once. WHEN to crack (now vs holding it for a landfall payoff) is the search's: the activation is an optional trailing action of the plan, and an activation with nothing left to find is a dominated self-sacrifice that is not offered autonomously. WHICH basic = the in-plan land-search pin; the human picks at resolution (`tutor_etb`; the board verb is landsearch=). NOT the fetchland collapse (fetch_land_types / PerformFetch), which never lets the land enter, pays 1 life, ignores 'tapped' and filters by subtype. PROVISIONAL: the instant-speed timing is collapsed to the trailing pass of our main phase -- against a passive opponent a crack in combat or on the opponent's turn gives the same board (the basic enters tapped either way; landfall counters placed in main 1 are on the attackers at damage). ORDER: the crack runs in the trailing pass, after the plan's casts (its landfall pumps the Plants a just-cast Avenger made); a plan that also casts an ETB that COUNTS lands (Avenger, Phylath) gets searched crack POSITIONS -- before every cast, or right after the k-th landfall-watcher cast (Plan::land_search_act_slot, in Plan::land_slot's encoding).
- coverage deferral -- Myriad Landscape: Enters tapped (enters_tapped); taps for {C} (produces [C
- coverage deferral -- Radiant Fountain: Untapped colourless utility land; both clauses modelled, nothing deferred. (1) 'You gain 2 life' -> etb_lifegain 2, fired by the land drop (PlayLandFromHand) and, for a land PUT onto the battlefield by an effect, by ResolveLandEnterEtbExtras (Genesis Wave's second pass, PerformLandSearch's second pass -- Primeval Titan) -- through the shared GainLife, so it is a real life-gain EVENT (CR 119.10). (2) '{T}: Add {C}' -> produces [C
- coverage deferral -- Genesis Wave: reveal_x_put_permanents: ONE gate param, because the reveal COUNT and the MV CAP are the SAME chosen X and the leftovers are MILLED. The Muxus params (etb_reveal_count / _put_max_mv) are fixed ints, subtype-filtered, rest-to-BOTTOM and fire from an ETB, so bending them into this would have cost four new flags to express one card. Shape precedent is PerformMuxusReveal; X threading is tutor_mv_max_is_x (Chord of Calling -- the cap reads off the SAME Action::chosen_x the enumeration used, never recomputed at resolution); simultaneity is PerformUpkeepSacTutor (Defense of the Heart). NOTHING DEFERRED. (1) SEARCH: X is a real cast axis, enumerated by its own branch in TurnSolver's {X} block -- without that branch the block's terminal `continue` (non-DirectDamage) would drop the card from enumeration ENTIRELY and it could never be cast (the Luxurious Libation trap). GenericProvider's max-X rule is kept (a bigger X is a strictly deeper reveal AND a strictly higher cap, so one variant per cast), which is also the only thing keeping a card castable at X=12 finite; MTG_UNPRUNED=xspell opens the full range for the A/B. Under HumanPlayActive() the branch emits EVERY legal X (0..max) because human play must be offered every legal X (GenericProvider::XCandidates' generic path does not check HumanPlayActive), which is WIDENING. (2) PERMANENT CARD = the six-type disjunction (Creature/Land/Enchantment/Artifact/Planeswalker/Battle), now the shared predicate Card::IsPermanentCard(); both the type test and the MV test read the DEFINITION via LookupCached, never the library card -- a library card is a placeholder with EMPTY type masks (the ExileTop/ApplyRadMill trap), so raw.IsCreature() is false for every card here and the Wave would put nothing, or everything. Lands are MV 0, so every land qualifies at any X >= 1: the Wave's floor is 'put your lands and dorks', never a whiff. Every instant/sorcery revealed is milled (in the Landfall 60, 12 of 60: 4 Search for Tomorrow, 4 Rampant Growth, 2 Cultivate and the other Genesis Wave), so a big X really does bin your own copies -- priced, not dodged. (3) 'ANY NUMBER' is a real choice, not 'all': DecisionProvider::GenesisWavePutPicks, a disclosed rule and NOT a searched subset (a searched subset is 2^X plan variants, and Turntimber's SINGLE named put already measured as this engine's #1 branching driver). Default: put every legal revealed permanent EXCEPT a creature whose MANDATORY ETB would destroy one of our own permanents -- a DUPLICATE LEGEND IS PUT (2026-10-15): the unpicked alternative is the same graveyard, and the put copy's ETB, its landfall triggers on the lands entering beside it, and its legend-rule DEATH (CR 704.5j / 700.4) all still happen; a Karoo is always PUT (worst case it returns itself to hand, which beats the graveyard). (4) SIMULTANEITY IS THE CARD. 'Put ... onto the battlefield' is ONE event, so every entrant's watchers are live before any enter-trigger resolves (the paired-Soul-Warden ruling, CR 603.6d). PerformGenesisWave uses TWO passes -- pass 1 appends every selected permanent, pass 2 re-finds each by per-copy m_number and fires FireEtbWatchers + FireOwnEtbTriggers in reveal order (CR 603.3b: the controller orders their own simultaneous triggers). A per-put sequential loop (PerformTutorToBattlefield's shape) UNDERCOUNTS, and here that is the whole payoff: a Wave putting N creatures with A Verdant Sun's Avatars live gains life A x N times (each Avatar sees every simultaneous entrant AND itself, own_creature_enters_lifegain_toughness + creature_enters_includes_self), and every one of those is a separate life-gain EVENT (CR 119.10) through the shared GainLife -- so a separate +1/+1 counter batch on each Ageless Entity and a separate Nykthos Paragon look. Re-find is by NUMBER, never battlefield.back(), because an earlier cascade can push tokens or sweep slots. (5) LANDS go through EnterLand, NOT the land drop -- a land put by an effect is not PLAYED, so lands_played_this_turn is untouched BY CONSTRUCTION (EnterLand's header: 'Does NOT touch the hand or the land-drop count'). Their own enter-triggers (etb_bounce_land, etb_lifegain) are DEFERRED TO PASS 2 through the shared ResolveLandEnterEtbExtras, exactly so they obey the same simultaneity: Blossoming Sands' 1 life is a gain event that must be seen by an Ageless Entity that entered ALONGSIDE it, and Selesnya Sanctuary's bounce is MANDATORY (skipping it would gift a free Karoo). A put Branchloft Pathway enters FRONT face up (CR 712.12), i.e. {G} only. A put land DOES trigger landfall (the trigger watches the zone change, not the land drop): PerformGenesisWave fires it once per placed land in a THIRD pass, after the enter triggers, through the shared chokepoint OnLandEntered, so every landfall watcher on the battlefield -- one put by this same Wave included -- sees every land (CR 603.6a). (6) GRAVEYARD: leftovers are pushed in reveal order. NO SHUFFLE -- this is not a search, and the tutor helpers' trailing ShuffleAfterSearch would both be a rules error and scramble the library the clairvoyant search reads. (7) CLAIRVOYANCE: the search already reads the real top, so 'reveal the top X' gives it no new information; and because this is a public REVEAL (not Turntimber's 'LOOK at'), showing the human the same cards leaks nothing -- EmitReveal labels each revealed card 'battlefield' or 'graveyard'. (8) Every put creature is summoning sick (entered_this_turn), so the board the Wave makes cannot attack this turn; the value is the lifegain burst landing on creatures ALREADY untapped, which is why MainPhasePlacement pulls it to Main1 (the Muxus ETB MASS-PUT rule) and why it does NOT open a second main. EnforceLegendRule runs once after the puts. PERF: this is the deck's most expensive card by a wide margin -- O(puts x watchers x board) at every rollout leaf, on FireCreatureEnterWatchers, already the hottest symbol in the engine; MTG_ENTER_WATCHER_GATE cannot help because the decklist HOLDS a creature-enter watcher.
- coverage deferral -- Phylath, World Sculptor: ETB: etb_tokens_per_land 1 + etb_tokens_per_land_basic_only + the etb_created_token_* spec (0/1, [Plant
- coverage deferral -- Courser of Kruphix: TOP-OF-LIBRARY LAND PLAY (clause 2, play_lands_from_library_top): while Courser is ours, the library TOP is a second source for the turn's land drop -- the SAME drop, not an extra one (2021-03-19 ruling) -- played through PlayLandFromLibraryTop, the hand drop's own body, so every land ETB and the landfall dispatch fire exactly as for a hand land. Searched as its own land option (Plan::land_from_top), BRANCHED and never folded into a same-name hand land, because the two leave different libraries and hands and so different futures; on an exact tie the top is ordered first (provider TopLandSourceFirstOnTie: it keeps the hand copy). A land arriving on top mid-turn while the drop is open (a shuffle, Genesis Wave, a draw, Courser itself entering) re-solves the turn (breakpoint site 10, TopLandNewlyPlayable). The search sees the post-shuffle order (it is clairvoyant through every shuffle; disclosed). REVEAL (clause 1, library_top_revealed): display-only in goldfish -- the search is already clairvoyant and the passive opponent reads nothing; the viewer shows the top face-up (library_top) and the human plays it with landtop=. KNOWN GAP: no autonomous main-2 drop for a land Primeval Titan's ATTACK trigger shuffles onto the top (human play has it). LANDFALL (clause 3): landfall_lifegain 1 -- one life-gain EVENT (CR 119.10) through the shared GainLife hook (it fires every 'whenever you gain life' watcher), once per land played or put (OnLandEntered). Life is near-inert for the goldfish clock (nothing damages us); the event matters only to lifegain readers. The 2/4 Enchantment Creature body is modelled.
- coverage deferral -- Gruul Turf: Karoo bounce land: enters tapped, makes 2 mana ({R}{G}: ONE OF EACH colour per tap, never two of either -- ManaPayment's Karoo bundle), and on ETB returns one of your lands to hand (BounceKarooLand prefers a tapped land; the replay cost is the real tempo cost). The Izzet Boilerworks pattern.
- coverage deferral -- Omnath, Locus of Rage: LANDFALL: landfall_creates_tokens 1 + landfall_token_* -- a 5/5 red AND green Elemental (landfall_token_color 'RG': BuildTokenProto adds every colour letter), mandatory, summoning sick, once per land played or put (OnLandEntered). DEATH TRIGGER: the Pashalik Mons shape -- dies_watch_subtype Elemental + dies_watch_includes_self + dies_trigger_damage 3, consumed by OnCreatureDies, which every death site reaches INCLUDING THE LEGEND RULE (EnforceLegendRule fires OnCreatureDies for each doomed creature; CR 704.5j puts it into the graveyard and CR 700.4 makes that a death): with Omnath out, a second Phylath, World Sculptor (a legendary Elemental) cast or put dies to the legend rule and Omnath deals 3. Avenger of Zendikar and Omnath's own tokens are Elementals too. 'Any target' is resolved to the OPPONENT'S FACE, the dies_trigger_damage precedent (Pashalik Mons, Slimefoot): the opponent's life is the only thing that advances the race, the passive opponent's creatures never block or attack, and damage to our own permanents only costs us -- even the one self-target with a payoff (an own Elemental of toughness <= 3, to re-trigger Omnath) ends with the same 3 to the face minus a body. The damage lands straight on opponent life, so every lethal check and rollout sees it. Legendary, one copy; the legend rule is modelled. [PARTIAL: 'any target' is not offered to the human as a board choice -- the dies_trigger_damage precedent, classified inert in scripts/audit_viewer_decisions.py; WHY inert: the dominance above (PROVISIONAL, user sign-off pending).
- coverage deferral -- Haunted Fengraf: The {C} mana ability via produces [C
- coverage deferral -- Terramorphic Expanse: A REAL battlefield land with NO mana ability: template custom, so no mana-source site counts it (a basic_land template with empty produces would tap for a phantom {C}). It ENTERS (its own landfall) and '{T}, Sacrifice: search' is a SEARCHED activation (PermAbilityMode::SacLandSearch, armed by land_search_activation_cost '' + land_search_activation_taps + land_search_activation_sacrifice_self): the {T} is paid first, the land is sacrificed as part of the cost (graveyard, the sacrifice watchers -- not a creature dying), then PerformLandSearch puts one basic onto the battlefield TAPPED through EnterLand (a second landfall) and shuffles once. WHEN to crack (now vs holding it for a landfall payoff) is the search's: the activation is an optional trailing action of the plan, and an activation with nothing left to find is a dominated self-sacrifice that is not offered autonomously. WHICH basic = the in-plan land-search pin; the human picks at resolution (`tutor_etb`; the board verb is landsearch=). NOT the fetchland collapse (fetch_land_types / PerformFetch), which never lets the land enter, pays 1 life, ignores 'tapped' and filters by subtype. PROVISIONAL: the instant-speed timing is collapsed to the trailing pass of our main phase -- against a passive opponent a crack in combat or on the opponent's turn gives the same board (the basic enters tapped either way; landfall counters placed in main 1 are on the attackers at damage). ORDER: the crack runs in the trailing pass, after the plan's casts (its landfall pumps the Plants a just-cast Avenger made); a plan that also casts an ETB that COUNTS lands (Avenger, Phylath) gets searched crack POSITIONS -- before every cast, or right after the k-th landfall-watcher cast (Plan::land_search_act_slot, in Plan::land_slot's encoding).
- coverage deferral -- Terastodon: The passive opponent controls no noncreature permanents in this sim, so the live mode destroys OUR OWN. USER heuristic 2026-08-20/21 (MTG_TERA_K): K is PROJECTED at resolution for BOTH entry paths (the cast rides the kEtbKxHeuristic sentinel like the puts -- no searched fan, measured identical to one), mid-plan where the board and remaining pool reflect the plan. Earliest winnable horizon, then the smallest K required for it: h=0 a Craterhoof still lands today (hand / live Call of the Wild off the top / Worldly Tutor+Call / Natural Order fetch) and the swing is lethal (sick Elephants only feed the Hoof's X); h=1 plain next-turn swing (an affordable power-4+ hand threat's power folds in, so keep-the-permanents = K 0 falls out); h=1' Craterhoof next turn (mana = all sources' yields minus K eaten plus a land drop) only if h=1 fails; h=2 two swings; else cap. MTG_UNPRUNED/terak or human play = the explicit 0..cap fan. (The v1 engine wired K but never emitted it, so no Elephant was ever made.) Victims widen past Forests ONLY when Forests run out: other lands, then Sol Ring, then other noncreatures, Call of the Wild LAST (Worldly Tutor into a Call activation is a real move). A PUT Terastodon (Natural Order / Call of the Wild / Turntimber / Genesis Wave) picks K at resolution through the SAME ProjectEtbDestroyK as the cast, cap fallback included (kEtbKxHeuristic; no searched axis there).
- coverage deferral -- Rampant Growth: land_search_on_resolve: the shared PerformLandSearch (src/core/LandSearch.h). One basic land card -- the Basic SUPERTYPE read off the definition (IsBasicLandCard), never the basic_land template -- onto the battlefield TAPPED through EnterLand(LandEntryTap::ForcedTapped), the shared land-entry chokepoint, so landfall fires and no shock life is paid; then ONE ShuffleAfterSearch, also on a fail-to-find. Not a land drop. WHICH basic is SEARCHED: the in-plan land-search pin (Plan::land_search_pins) indexes the provider-ordered LandSearchPicks list computed at resolution; copies of one basic are one choice (identity fold). The human picks at resolution (the `tutor_etb` prompt; -1 = find nothing). 'May fail to find' is not offered to the search: a free tapped basic is never worse than none against a passive opponent, and the per-card shuffle key leaves every other card's order unchanged, so the only effect of the find is one land now and one card removed from an otherwise identical future (PROVISIONAL collapse, human keeps the decline).
- allowlisted divergence -- Galerider Sliver [keywords]: Keyword-lord: 'Sliver creatures you control have flying' grants flying to your Slivers INCLUDING itself, so the card functionally has flying (modeled 
- allowlisted divergence -- Striking Sliver [keywords]: Keyword-lord: grants first strike to your Slivers incl. itself (modeled self-innate). First strike is inert in goldfishing (no blockers). See oracle b
- allowlisted divergence -- Cloudshredder Sliver [keywords]: Keyword-lord: grants flying+haste to your Slivers incl. itself. Flying self-innate + inert in goldfishing; haste additionally granted to other Slivers
- allowlisted divergence -- Haytham Kenway [keywords]: 'Protection from Assassins' is a real keyword but inert in goldfishing (no Assassins in play); the protection-to-other-Knights is an anthem grant, not
- allowlisted divergence -- Goblin Piledriver [keywords]: 'Protection from blue' is a real keyword but inert in goldfishing (the passive opponent has no blue sources or blockers to target); the attack-trigger
- allowlisted divergence -- Progenitus [keywords]: 'Protection from everything' is a real keyword but inert in goldfishing (the passive opponent never targets, blocks, or damages); the graveyard shuffl
- allowlisted divergence -- Bloom Tender [keywords]: Scryfall lists 'vivid' in keywords -- a data quirk (no rules-meaningful innate keyword on this card); the each-color-among-permanents mana ability is 
- allowlisted divergence -- Auriok Champion [keywords]: 'Protection from black and from red' is a real keyword but inert in goldfishing on all four DEBT axes (the passive opponent never damages, targets, bl
- allowlisted divergence -- Brightcap Badger [mana_cost]: ADVENTURE CARD (CR 715), authored as TWO entries -- 'Brightcap Badger' (the {3}{G} 3/4 creature face) and 'Fungus Frolic' (the {2}{G} instant half). S
- allowlisted divergence -- Fungus Frolic [mana_cost]: The ADVENTURE HALF of Brightcap Badger // Fungus Frolic -- see the Brightcap Badger entry. Scryfall's combined mana_cost is '{3}{G} // {2}{G}'; the in
- allowlisted divergence -- Fungus Frolic [P/T]: An Instant has no power or toughness. Scryfall reports 3/4 because it describes the COMBINED card, whose creature face is the 3/4 Brightcap Badger. Gi
- allowlisted divergence -- Mycoloth [keywords]: 'Devour 2' is a real keyword and is FULLY MODELLED -- but via the `devour` CardParam, not via Keyword::Devour. The engine's Keyword enum deliberately 
- allowlisted divergence -- Brutal Cathar [keywords]: Transforming DFC (Brutal Cathar // Moonrage Brute): Scryfall reports CARD-LEVEL keywords for both faces. The front face's only keyword is Daybound, mo
- allowlisted divergence -- Moonrage Brute [keywords]: Transforming DFC back face: Scryfall's card-level keywords include the front's Daybound and the layout's Transform; Nightbound is modelled structurall
- STALE allowlist entry -- Glorybringer [keywords] no longer mismatches; remove from scryfall_divergences.json
- oracle_text advisory -- Gideon, Ally of Zendikar: oracle_text diverges (similarity 0.10); scryfall='+1: Until end of turn, Gideon becomes a 5/5 Human Soldier Ally creature with indestructible that\'s still a pl
- oracle_text advisory -- Basri, Tomorrow's Champion: oracle_text diverges (similarity 0.15); scryfall="{W}, {T}, Exert Basri: Create a 1/1 white Cat creature token with lifelink. (An exerted creature won't untap d
- oracle_text advisory -- Light Up the Stage: oracle_text diverges (similarity 0.69); scryfall='Spectacle {R} (You may cast this spell for its spectacle cost rather than its mana cost if an opponent lost li
- oracle_text advisory -- Crystalline Sliver: oracle_text diverges (similarity 0.61); scryfall="All Slivers have shroud. (They can't be the targets of spells or abilities.)"
- oracle_text advisory -- Galerider Sliver: oracle_text diverges (similarity 0.41); scryfall='Sliver creatures you control have flying.'
- oracle_text advisory -- Striking Sliver: oracle_text diverges (similarity 0.56); scryfall='Sliver creatures you control have first strike. (They deal combat damage before creatures without first strike
- oracle_text advisory -- Cloudshredder Sliver: oracle_text diverges (similarity 0.48); scryfall='Sliver creatures you control have flying and haste.'
- oracle_text advisory -- Hibernation Sliver: oracle_text diverges (similarity 0.49); scryfall='All Slivers have "Pay 2 life: Return this permanent to its owner\'s hand."'
- oracle_text advisory -- Cavern of Souls: oracle_text diverges (similarity 0.40); scryfall="As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Unclaimed Territory: oracle_text diverges (similarity 0.20); scryfall='As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Secluded Courtyard: oracle_text diverges (similarity 0.27); scryfall='As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Mutavault: oracle_text diverges (similarity 0.56); scryfall="{T}: Add {C}.\n{1}: This land becomes a 2/2 creature with all creature types until end of turn. It's still a l
- oracle_text advisory -- Aether Vial: oracle_text diverges (similarity 0.71); scryfall='At the beginning of your upkeep, you may put a charge counter on this artifact.\n{T}: You may put a creature c
- oracle_text advisory -- Reliquary Tower: oracle_text diverges (similarity 0.44); scryfall='You have no maximum hand size.\n{T}: Add {C}.'
- oracle_text advisory -- Dwarven Hold: oracle_text diverges (similarity 0.23); scryfall='This land enters tapped.\nYou may choose not to untap this land during your untap step.\nAt the beginning of y
- oracle_text advisory -- Mercadian Bazaar: oracle_text diverges (similarity 0.26); scryfall='This land enters tapped.\n{T}: Put a storage counter on this land.\n{T}, Remove any number of storage counters
- oracle_text advisory -- Temple of Epiphany: oracle_text diverges (similarity 0.60); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Thundering Falls: oracle_text diverges (similarity 0.63); scryfall='({T}: Add {U} or {R}.)\nThis land enters tapped.\nWhen this land enters, surveil 1. (Look at the top card of y
- oracle_text advisory -- Land's Edge: oracle_text diverges (similarity 0.51); scryfall='Discard a card: If the discarded card was a land card, this enchantment deals 2 damage to target player or pla
- oracle_text advisory -- Throes of Chaos: oracle_text diverges (similarity 0.06); scryfall='Cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland card tha
- oracle_text advisory -- Tournament Grounds: oracle_text diverges (similarity 0.37); scryfall='{T}: Add {C}.\n{T}: Add {R}, {W}, or {B}. Spend this mana only to cast a Knight or Equipment spell.'
- oracle_text advisory -- Dauntless Bodyguard: oracle_text diverges (similarity 0.55); scryfall='As this creature enters, choose another creature you control.\nSacrifice this creature: The chosen creature ga
- oracle_text advisory -- Venerable Knight: oracle_text diverges (similarity 0.52); scryfall='When this creature dies, put a +1/+1 counter on target Knight you control.'
- oracle_text advisory -- Acclaimed Contender: oracle_text diverges (similarity 0.45); scryfall='When this creature enters, if you control another Knight, look at the top five cards of your library. You may 
- oracle_text advisory -- Knight Exemplar: oracle_text diverges (similarity 0.41); scryfall='First strike (This creature deals combat damage before creatures without first strike.)\nOther Knight creature
- oracle_text advisory -- Valiant Knight: oracle_text diverges (similarity 0.18); scryfall='Other Knights you control get +1/+1.\n{3}{W}{W}: Knights you control gain double strike until end of turn.'
- oracle_text advisory -- Kinsbaile Cavalier: oracle_text diverges (similarity 0.06); scryfall='Knight creatures you control have double strike.'
- oracle_text advisory -- Marshal of Zhalfir: oracle_text diverges (similarity 0.49); scryfall='Other Knights you control get +1/+1.\n{W}{U}, {T}: Tap another target creature.'
- oracle_text advisory -- Haytham Kenway: oracle_text diverges (similarity 0.53); scryfall='Protection from Assassins\nOther Knights you control get +2/+2 and have protection from Assassins.\nWhen Hayth
- oracle_text advisory -- Adeline, Resplendent Cathar: oracle_text diverges (similarity 0.76); scryfall="Vigilance\nAdeline's power is equal to the number of creatures you control.\nWhenever you attack, for each opp
- oracle_text advisory -- Accorder Paladin: oracle_text diverges (similarity 0.33); scryfall='Battle cry (Whenever this creature attacks, each other attacking creature gets +1/+0 until end of turn.)'
- oracle_text advisory -- Silverblade Paladin: oracle_text diverges (similarity 0.25); scryfall='Soulbond (You may pair this creature with another unpaired creature when either enters. They remain paired for
- oracle_text advisory -- Hero of Bladehold: oracle_text diverges (similarity 0.50); scryfall='Battle cry (Whenever this creature attacks, each other attacking creature gets +1/+0 until end of turn.)\nWhen
- oracle_text advisory -- Windswept Heath: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Plains card, put it onto the battlef
- oracle_text advisory -- Marsh Flats: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Plains or Swamp card, put it onto the battlefi
- oracle_text advisory -- Bloodstained Mire: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Mountain card, put it onto the battle
- oracle_text advisory -- Wooded Foothills: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Mountain or Forest card, put it onto the battl
- oracle_text advisory -- Grove of the Burnwillows: oracle_text diverges (similarity 0.20); scryfall='{T}: Add {C}.\n{T}: Add {R} or {G}. Each opponent gains 1 life.'
- oracle_text advisory -- Ignoble Hierarch: oracle_text diverges (similarity 0.56); scryfall='Exalted (Whenever a creature you control attacks alone, that creature gets +1/+1 until end of turn.)\n{T}: Add
- oracle_text advisory -- Skyshroud Cutter: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 5 life."
- oracle_text advisory -- Plague Drone: oracle_text diverges (similarity 0.70); scryfall='Flying\nRot Fly — If an opponent would gain life, that player loses that much life instead.'
- oracle_text advisory -- Aria of Flame: oracle_text diverges (similarity 0.78); scryfall='When this enchantment enters, each opponent gains 10 life.\nWhenever you cast an instant or sorcery spell, put
- oracle_text advisory -- Fiery Justice: oracle_text diverges (similarity 0.54); scryfall='Fiery Justice deals 5 damage divided as you choose among any number of targets. Target opponent gains 5 life.'
- oracle_text advisory -- Swords to Plowshares: oracle_text diverges (similarity 0.17); scryfall='Exile target creature. Its controller gains life equal to its power.'
- oracle_text advisory -- Invigorate: oracle_text diverges (similarity 0.52); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have an opponent gain 3 life.\nTarget
- oracle_text advisory -- Reverent Silence: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 6 life.\n
- oracle_text advisory -- Idyllic Tutor: oracle_text diverges (similarity 0.43); scryfall='Search your library for an enchantment card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Enlightened Tutor: oracle_text diverges (similarity 0.55); scryfall='Search your library for an artifact or enchantment card, reveal it, then shuffle and put that card on top.'
- oracle_text advisory -- Forbidden Orchard: oracle_text diverges (similarity 0.22); scryfall='{T}: Add one mana of any color.\nWhenever you tap this land for mana, target opponent creates a 1/1 colorless 
- oracle_text advisory -- Reflecting Pool: oracle_text diverges (similarity 0.26); scryfall='{T}: Add one mana of any type that a land you control could produce.'
- oracle_text advisory -- Izzet Signet: oracle_text diverges (similarity 0.11); scryfall='{1}, {T}: Add {U}{R}.'
- oracle_text advisory -- Ponder: oracle_text diverges (similarity 0.32); scryfall='Look at the top three cards of your library, then put them back in any order. You may shuffle.\nDraw a card.'
- oracle_text advisory -- Preordain: oracle_text diverges (similarity 0.27); scryfall='Scry 2, then draw a card. (To scry 2, look at the top two cards of your library, then put any number of them o
- oracle_text advisory -- Expressive Iteration: oracle_text diverges (similarity 0.45); scryfall='Look at the top three cards of your library. Put one of them into your hand, put one of them on the bottom of 
- oracle_text advisory -- Crackle with Power: oracle_text diverges (similarity 0.17); scryfall='Crackle with Power deals five times X damage to each of up to X targets.'
- oracle_text advisory -- Remand: oracle_text diverges (similarity 0.58); scryfall="Counter target spell. If that spell is countered this way, put it into its owner's hand instead of into that p
- oracle_text advisory -- Memory Lapse: oracle_text diverges (similarity 0.69); scryfall="Counter target spell. If that spell is countered this way, put it on top of its owner's library instead of int
- oracle_text advisory -- Distorting Wake: oracle_text diverges (similarity 0.34); scryfall="Return X target nonland permanents to their owners' hands."
- oracle_text advisory -- Icy Blast: oracle_text diverges (similarity 0.64); scryfall="Tap X target creatures.\nFerocious — If you control a creature with power 4 or greater, those creatures don't 
- oracle_text advisory -- Hinata, Dawn-Crowned: oracle_text diverges (similarity 0.31); scryfall='Flying, trample\nSpells you cast cost {1} less to cast for each target.\nSpells your opponents cast cost {1} m
- oracle_text advisory -- Izzet Boilerworks: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {U}{
- oracle_text advisory -- Orzhov Basilica: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {W}{
- oracle_text advisory -- Soulfire Eruption: oracle_text diverges (similarity 0.28); scryfall="Choose any number of target creatures, planeswalkers, and/or players. For each of them, exile the top card of 
- oracle_text advisory -- Magma Opus: oracle_text diverges (similarity 0.46); scryfall='Magma Opus deals 4 damage divided as you choose among any number of targets. Tap two target permanents. Create
- oracle_text advisory -- Reality Spasm: oracle_text diverges (similarity 0.20); scryfall='Choose one —\n• Tap X target permanents.\n• Untap X target permanents.'
- oracle_text advisory -- Ornithopter of Paradise: oracle_text diverges (similarity 0.13); scryfall='Flying\n{T}: Add one mana of any color.'
- oracle_text advisory -- Gamble: oracle_text diverges (similarity 0.22); scryfall='Search your library for a card, put that card into your hand, discard a card at random, then shuffle.'
- oracle_text advisory -- Irencrag Feat: oracle_text diverges (similarity 0.10); scryfall='Add seven {R}. You can cast only one more spell this turn.'
- oracle_text advisory -- Pyretic Ritual: oracle_text diverges (similarity 0.05); scryfall='Add {R}{R}{R}.'
- oracle_text advisory -- Seething Song: oracle_text diverges (similarity 0.08); scryfall='Add {R}{R}{R}{R}{R}.'
- oracle_text advisory -- Desperate Ritual: oracle_text diverges (similarity 0.18); scryfall="Add {R}{R}{R}.\nSplice onto Arcane {1}{R} (As you cast an Arcane spell, you may reveal this card from your han
- oracle_text advisory -- Dragonlord Kolaghan: oracle_text diverges (similarity 0.53); scryfall='Flying, haste\nOther creatures you control have haste.\nWhenever an opponent casts a creature or planeswalker 
- oracle_text advisory -- Karrthus, Tyrant of Jund: oracle_text diverges (similarity 0.37); scryfall='Flying, haste\nWhen Karrthus enters, gain control of all Dragons, then untap all Dragons.\nOther Dragon creatu
- oracle_text advisory -- Ruby Medallion: oracle_text diverges (similarity 0.17); scryfall='Red spells you cast cost {1} less to cast.'
- oracle_text advisory -- Lotus Bloom: oracle_text diverges (similarity 0.25); scryfall='Suspend 3—{0} (Rather than cast this card from your hand, pay {0} and exile it with three time counters on it.
- oracle_text advisory -- Rite of Flame: oracle_text diverges (similarity 0.21); scryfall='Add {R}{R}, then add {R} for each card named Rite of Flame in each graveyard.'
- oracle_text advisory -- Scourge of Valkas: oracle_text diverges (similarity 0.32); scryfall='Flying\nWhenever this creature or another Dragon you control enters, it deals X damage to any target, where X 
- oracle_text advisory -- Lathliss, Dragon Queen: oracle_text diverges (similarity 0.31); scryfall='Flying\nWhenever another nontoken Dragon you control enters, create a 5/5 red Dragon creature token with flyin
- oracle_text advisory -- Utvara Hellkite: oracle_text diverges (similarity 0.24); scryfall='Flying\nWhenever a Dragon you control attacks, create a 6/6 red Dragon creature token with flying.'
- oracle_text advisory -- Dragonstorm: oracle_text diverges (similarity 0.16); scryfall='Search your library for a Dragon permanent card, put it onto the battlefield, then shuffle.\nStorm (When you c
- oracle_text advisory -- Apex of Power: oracle_text diverges (similarity 0.15); scryfall='Exile the top seven cards of your library. Until end of turn, you may cast spells from among them.\nIf this sp
- oracle_text advisory -- Slippery Bogle: oracle_text diverges (similarity 0.41); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Gladecover Scout: oracle_text diverges (similarity 0.76); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Kor Spiritdancer: oracle_text diverges (similarity 0.53); scryfall='This creature gets +2/+2 for each Aura attached to it.\nWhenever you cast an Aura spell, you may draw a card.'
- oracle_text advisory -- Light-Paws, Emperor's Voice: oracle_text diverges (similarity 0.74); scryfall='Whenever an Aura you control enters, if you cast it, you may search your library for an Aura card with mana va
- oracle_text advisory -- Ethereal Armor: oracle_text diverges (similarity 0.60); scryfall='Enchant creature\nEnchanted creature gets +1/+1 for each enchantment you control and has first strike.'
- oracle_text advisory -- Rancor: oracle_text diverges (similarity 0.68); scryfall="Enchant creature\nEnchanted creature gets +2/+0 and has trample.\nWhen this Aura is put into a graveyard from 
- oracle_text advisory -- Daybreak Coronet: oracle_text diverges (similarity 0.58); scryfall='Enchant creature with another Aura attached to it\nEnchanted creature gets +3/+3 and has first strike, vigilan
- oracle_text advisory -- Armadillo Cloak: oracle_text diverges (similarity 0.77); scryfall='Enchant creature\nEnchanted creature gets +2/+2 and has trample.\nWhenever enchanted creature deals damage, yo
- oracle_text advisory -- Spirit Mantle: oracle_text diverges (similarity 0.66); scryfall='Enchant creature\nEnchanted creature gets +1/+1 and has protection from creatures.'
- oracle_text advisory -- Spider Umbra: oracle_text diverges (similarity 0.40); scryfall='Enchant creature\nEnchanted creature gets +1/+1 and has reach. (It can block creatures with flying.)\nUmbra ar
- oracle_text advisory -- Ancestral Mask: oracle_text diverges (similarity 0.59); scryfall='Enchant creature\nEnchanted creature gets +2/+2 for each other enchantment on the battlefield.'
- oracle_text advisory -- Alpha Authority: oracle_text diverges (similarity 0.54); scryfall="Enchant creature\nEnchanted creature has hexproof and can't be blocked by more than one creature."
- oracle_text advisory -- Gryff's Boon: oracle_text diverges (similarity 0.75); scryfall='Enchant creature\nEnchanted creature gets +1/+0 and has flying.\n{3}{W}: Return this card from your graveyard 
- oracle_text advisory -- Audacity: oracle_text diverges (similarity 0.59); scryfall="Enchant creature\nEnchanted creature gets +2/+0 and has trample. (It can deal excess combat damage to the play
- oracle_text advisory -- All That Glitters: oracle_text diverges (similarity 0.57); scryfall='Enchant creature\nEnchanted creature gets +1/+1 for each artifact and/or enchantment you control.'
- oracle_text advisory -- Spirit Link: oracle_text diverges (similarity 0.47); scryfall='Enchant creature (Target a creature as you cast this. This card enters attached to that creature.)\nWhenever e
- oracle_text advisory -- Lion Umbra: oracle_text diverges (similarity 0.77); scryfall='Enchant modified creature (Equipment, Auras its controller controls, and counters are modifications.)\nEnchant
- oracle_text advisory -- Brushland: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.\n{T}: Add {G} or {W}. This land deals 1 damage to you.'
- oracle_text advisory -- Branchloft Pathway: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Darkbore Pathway: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {B}.'
- oracle_text advisory -- Goblin King: oracle_text diverges (similarity 0.31); scryfall='Other Goblins get +1/+1 and have mountainwalk.'
- oracle_text advisory -- Goblin Chieftain: oracle_text diverges (similarity 0.41); scryfall='Haste (This creature can attack and {T} as soon as it comes under your control.)\nOther Goblin creatures you c
- oracle_text advisory -- Goblin Warchief: oracle_text diverges (similarity 0.52); scryfall='Goblin spells you cast cost {1} less to cast.\nGoblins you control have haste.'
- oracle_text advisory -- Goblin Piledriver: oracle_text diverges (similarity 0.43); scryfall="Protection from blue (This creature can't be blocked, targeted, dealt damage, or enchanted by anything blue.)\
- oracle_text advisory -- Goblin Matron: oracle_text diverges (similarity 0.66); scryfall='When this creature enters, you may search your library for a Goblin card, reveal that card, put it into your h
- oracle_text advisory -- Mogg War Marshal: oracle_text diverges (similarity 0.56); scryfall='Echo {1}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Siege-Gang Commander: oracle_text diverges (similarity 0.58); scryfall='When this creature enters, create three 1/1 red Goblin creature tokens.\n{1}{R}, Sacrifice a Goblin: This crea
- oracle_text advisory -- Skirk Prospector: oracle_text diverges (similarity 0.31); scryfall='Sacrifice a Goblin: Add {R}.'
- oracle_text advisory -- Krenko, Mob Boss: oracle_text diverges (similarity 0.43); scryfall='{T}: Create X 1/1 red Goblin creature tokens, where X is the number of Goblins you control.'
- oracle_text advisory -- Pashalik Mons: oracle_text diverges (similarity 0.52); scryfall='Whenever Pashalik Mons or another Goblin you control dies, Pashalik Mons deals 1 damage to any target.\n{3}{R}
- oracle_text advisory -- Rundvelt Hordemaster: oracle_text diverges (similarity 0.36); scryfall="Other Goblins you control get +1/+1.\nWhenever this creature or another Goblin you control dies, exile the top
- oracle_text advisory -- Goblin Lackey: oracle_text diverges (similarity 0.56); scryfall='Whenever this creature deals damage to a player, you may put a Goblin permanent card from your hand onto the b
- oracle_text advisory -- Muxus, Goblin Grandee: oracle_text diverges (similarity 0.08); scryfall='When Muxus enters, reveal the top six cards of your library. Put all Goblin creature cards with mana value 5 o
- oracle_text advisory -- Goblin Chainwhirler: oracle_text diverges (similarity 0.40); scryfall='First strike\nWhen this creature enters, it deals 1 damage to each opponent and each creature and planeswalker
- oracle_text advisory -- Twinshot Sniper: oracle_text diverges (similarity 0.50); scryfall='Reach\nWhen this creature enters, it deals 2 damage to any target.\nChannel — {1}{R}, Discard this card: It de
- oracle_text advisory -- Stingscourger: oracle_text diverges (similarity 0.71); scryfall="Echo {3}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Three Tree City: oracle_text diverges (similarity 0.47); scryfall='As Three Tree City enters, choose a creature type.\n{T}: Add {C}.\n{2}, {T}: Choose a color. Add an amount of 
- oracle_text advisory -- Hunted Phantasm: oracle_text diverges (similarity 0.34); scryfall="This creature can't be blocked.\nWhen this creature enters, target opponent creates five 1/1 red Goblin creatu
- oracle_text advisory -- Suture Priest: oracle_text diverges (similarity 0.49); scryfall='Whenever another creature you control enters, you may gain 1 life.\nWhenever a creature an opponent controls e
- oracle_text advisory -- Massacre Wurm: oracle_text diverges (similarity 0.38); scryfall='When this creature enters, creatures your opponents control get -2/-2 until end of turn.\nWhenever a creature 
- oracle_text advisory -- Soul Warden: oracle_text diverges (similarity 0.15); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- Essence Warden: oracle_text diverges (similarity 0.24); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- City of Brass: oracle_text diverges (similarity 0.19); scryfall='Whenever this land becomes tapped, it deals 1 damage to you.\n{T}: Add one mana of any color.'
- oracle_text advisory -- Defense of the Heart: oracle_text diverges (similarity 0.43); scryfall='At the beginning of your upkeep, if an opponent controls three or more creatures, sacrifice this enchantment, 
- oracle_text advisory -- Sylvan Scrying: oracle_text diverges (similarity 0.47); scryfall='Search your library for a land card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Crop Rotation: oracle_text diverges (similarity 0.42); scryfall='As an additional cost to cast this spell, sacrifice a land.\nSearch your library for a land card, put that car
- oracle_text advisory -- Varchild's War-Riders: oracle_text diverges (similarity 0.58); scryfall='Cumulative upkeep—Have an opponent create a 1/1 red Survivor creature token. (At the beginning of your upkeep,
- oracle_text advisory -- Azorius Chancery: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {W}{
- oracle_text advisory -- Tree of Tales: oracle_text diverges (similarity 0.15); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Misty Rainforest: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Island card, put it onto the battlef
- oracle_text advisory -- Verdant Catacombs: oracle_text diverges (similarity 0.28); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Forest card, put it onto the battlefi
- oracle_text advisory -- Scalding Tarn: oracle_text diverges (similarity 0.29); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for an Island or Mountain card, put it onto the batt
- oracle_text advisory -- Cosmic Spider-Man: oracle_text diverges (similarity 0.47); scryfall='Flying, first strike, trample, lifelink, haste\nAt the beginning of combat on your turn, other Spiders you con
- oracle_text advisory -- Mana Cannons: oracle_text diverges (similarity 0.44); scryfall='Whenever you cast a multicolored spell, this enchantment deals X damage to any target, where X is the number o
- oracle_text advisory -- Ancient Cornucopia: oracle_text diverges (similarity 0.43); scryfall="Whenever you cast a spell that's one or more colors, you may gain 1 life for each of that spell's colors. Do t
- oracle_text advisory -- Two-Headed Hellkite: oracle_text diverges (similarity 0.26); scryfall='Flying, menace, haste\nWhenever this creature attacks, draw two cards.'
- oracle_text advisory -- Progenitus: oracle_text diverges (similarity 0.28); scryfall="Protection from everything\nIf Progenitus would be put into a graveyard from anywhere, reveal Progenitus and s
- oracle_text advisory -- Faeburrow Elder: oracle_text diverges (similarity 0.36); scryfall='Vigilance\nThis creature gets +1/+1 for each color among permanents you control.\n{T}: For each color among pe
- oracle_text advisory -- Bloom Tender: oracle_text diverges (similarity 0.52); scryfall='Vivid — {T}: For each color among permanents you control, add one mana of that color.'
- oracle_text advisory -- Deathrite Shaman: oracle_text diverges (similarity 0.46); scryfall='{T}: Exile target land card from a graveyard. Add one mana of any color. (Activate only as an instant.)\n{B}, 
- oracle_text advisory -- Lightning Greaves: oracle_text diverges (similarity 0.18); scryfall="Equipped creature has haste and shroud. (It can't be the target of spells or abilities.)\nEquip {0}"
- oracle_text advisory -- Maelstrom Archangel: oracle_text diverges (similarity 0.31); scryfall='Flying\nWhenever this creature deals combat damage to a player, you may cast a spell from your hand without pa
- oracle_text advisory -- Jared Carthalion: oracle_text diverges (similarity 0.60); scryfall="+1: Create a 3/3 Kavu creature token with trample that's all colors.\n−3: Choose up to two target creatures. F
- oracle_text advisory -- Nicol Bolas, Planeswalker: oracle_text diverges (similarity 0.21); scryfall="+3: Destroy target noncreature permanent.\n−2: Gain control of target creature.\n−9: Nicol Bolas deals 7 damag
- oracle_text advisory -- Oko, Thief of Crowns: oracle_text diverges (similarity 0.42); scryfall='+2: Create a Food token. (It\'s an artifact with "{2}, {T}, Sacrifice this token: You gain 3 life.")\n+1: Targ
- oracle_text advisory -- Garth One-Eye: oracle_text diverges (similarity 0.39); scryfall="{T}: Choose a card name that hasn't been chosen from among Disenchant, Braingeyser, Terror, Shivan Dragon, Reg
- oracle_text advisory -- Black Lotus: oracle_text diverges (similarity 0.36); scryfall='{T}, Sacrifice this artifact: Add three mana of any one color.'
- oracle_text advisory -- Braingeyser: oracle_text diverges (similarity 0.23); scryfall='Target player draws X cards.'
- oracle_text advisory -- Terror: oracle_text diverges (similarity 0.32); scryfall="Destroy target nonartifact, nonblack creature. It can't be regenerated."
- oracle_text advisory -- Shivan Dragon: oracle_text diverges (similarity 0.32); scryfall='Flying\n{R}: This creature gets +1/+0 until end of turn.'
- oracle_text advisory -- Regrowth: oracle_text diverges (similarity 0.46); scryfall='Return target card from your graveyard to your hand.'
- oracle_text advisory -- Unite the Coalition: oracle_text diverges (similarity 0.46); scryfall="Choose five. You may choose the same mode more than once.\n• Target permanent phases out.\n• Target player dra
- oracle_text advisory -- Disenchant: oracle_text diverges (similarity 0.21); scryfall='Destroy target artifact or enchantment.'
- oracle_text advisory -- Mirrorwing Dragon: oracle_text diverges (similarity 0.42); scryfall='Flying\nWhenever a player casts an instant or sorcery spell that targets only this creature, that player copie
- oracle_text advisory -- Zada, Hedron Grinder: oracle_text diverges (similarity 0.57); scryfall='Whenever you cast an instant or sorcery spell that targets only Zada, copy that spell for each other creature 
- oracle_text advisory -- Goblin Instigator: oracle_text diverges (similarity 0.32); scryfall='When this creature enters, create a 1/1 red Goblin creature token.'
- oracle_text advisory -- Fists of Flame: oracle_text diverges (similarity 0.36); scryfall="Draw a card. Until end of turn, target creature gains trample and gets +1/+0 for each card you've drawn this t
- oracle_text advisory -- Luxurious Libation: oracle_text diverges (similarity 0.24); scryfall='Target creature gets +X/+X until end of turn. Create a 1/1 green and white Citizen creature token.'
- oracle_text advisory -- Fortifying Draught: oracle_text diverges (similarity 0.33); scryfall='You gain 2 life. Target creature gets +X/+X until end of turn, where X is the amount of life you gained this t
- oracle_text advisory -- Gold Rush: oracle_text diverges (similarity 0.36); scryfall='Create a Treasure token. Until end of turn, up to one target creature gets +2/+2 for each Treasure you control
- oracle_text advisory -- Ancestral Anger: oracle_text diverges (similarity 0.52); scryfall='Target creature gains trample and gets +X/+0 until end of turn, where X is 1 plus the number of cards named An
- oracle_text advisory -- Oracle's Restoration: oracle_text diverges (similarity 0.10); scryfall='Target creature you control gets +1/+1 until end of turn. You draw a card and gain 1 life.'
- oracle_text advisory -- Expedite: oracle_text diverges (similarity 0.29); scryfall='Target creature gains haste until end of turn.\nDraw a card.'
- oracle_text advisory -- Impolite Entrance: oracle_text diverges (similarity 0.18); scryfall='Target creature gains trample and haste until end of turn.\nDraw a card.'
- oracle_text advisory -- Scale the Heights: oracle_text diverges (similarity 0.49); scryfall='Put a +1/+1 counter on up to one target creature. You gain 2 life. You may play an additional land this turn.\
- oracle_text advisory -- Twinflame: oracle_text diverges (similarity 0.42); scryfall="Strive — This spell costs {2}{R} more to cast for each target beyond the first.\nChoose any number of target c
- oracle_text advisory -- Gruul Turf: oracle_text diverges (similarity 0.39); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {R}{
- oracle_text advisory -- Kazandu Refuge: oracle_text diverges (similarity 0.50); scryfall='This land enters tapped.\nWhen this land enters, you gain 1 life.\n{T}: Add {R} or {G}.'
- oracle_text advisory -- Rootbound Crag: oracle_text diverges (similarity 0.43); scryfall='This land enters tapped unless you control a Mountain or a Forest.\n{T}: Add {R} or {G}.'
- oracle_text advisory -- Colossus Hammer: oracle_text diverges (similarity 0.25); scryfall='Equipped creature gets +10/+10 and loses flying.\nEquip {8} ({8}: Attach to target creature you control. Equip
- oracle_text advisory -- Loxodon Warhammer: oracle_text diverges (similarity 0.36); scryfall='Equipped creature gets +3/+0 and has trample and lifelink.\nEquip {3}'
- oracle_text advisory -- Shadowspear: oracle_text diverges (similarity 0.54); scryfall='Equipped creature gets +1/+1 and has trample and lifelink.\n{1}: Permanents your opponents control lose hexpro
- oracle_text advisory -- Grafted Wargear: oracle_text diverges (similarity 0.52); scryfall='Equipped creature gets +3/+2.\nWhenever this Equipment becomes unattached from a permanent, sacrifice that per
- oracle_text advisory -- O-Naginata: oracle_text diverges (similarity 0.49); scryfall='This Equipment can be attached only to a creature with power 3 or greater.\nEquipped creature gets +3/+0 and h
- oracle_text advisory -- Ancient Den: oracle_text diverges (similarity 0.02); scryfall='{T}: Add {W}.'
- oracle_text advisory -- Bone Saw: oracle_text diverges (similarity 0.29); scryfall='Equipped creature gets +1/+0.\nEquip {1} ({1}: Attach to target creature you control. Equip only as a sorcery.
- oracle_text advisory -- Cathar's Shield: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +0/+3 and has vigilance.\nEquip {3} ({3}: Attach to target creature you control. Equip 
- oracle_text advisory -- Accorder's Shield: oracle_text diverges (similarity 0.23); scryfall="Equipped creature gets +0/+3 and has vigilance. (Attacking doesn't cause it to tap.)\nEquip {3} ({3}: Attach t
- oracle_text advisory -- Kite Shield: oracle_text diverges (similarity 0.52); scryfall='Equipped creature gets +0/+3.\nEquip {3} ({3}: Attach to target creature you control. Equip only as a sorcery.
- oracle_text advisory -- Spidersilk Net: oracle_text diverges (similarity 0.22); scryfall='Equipped creature gets +0/+2 and has reach. (It can block creatures with flying.)\nEquip {2} ({2}: Attach to t
- oracle_text advisory -- Golem-Skin Gauntlets: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +1/+0 for each Equipment attached to it.\nEquip {2} ({2}: Attach to target creature you
- oracle_text advisory -- Skateboard: oracle_text diverges (similarity 0.19); scryfall='When this Equipment enters, tap target permanent.\nEquipped creature gets +1/+0 and has haste.\nEquip {1} ({1}
- oracle_text advisory -- Dragonfire Blade: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +2/+2 and has hexproof from monocolored.\nEquip {4}. This ability costs {1} less to act
- oracle_text advisory -- Deconstruction Hammer: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +1/+1 and has "{3}, {T}, Sacrifice Deconstruction Hammer: Destroy target artifact or en
- oracle_text advisory -- Sram, Senior Edificer: oracle_text diverges (similarity 0.08); scryfall='Whenever you cast an Aura, Equipment, or Vehicle spell, draw a card.'
- oracle_text advisory -- Cid, Freeflier Pilot: oracle_text diverges (similarity 0.13); scryfall='Equipment and Vehicle spells you cast cost {1} less to cast.\nJump — During your turn, Cid has flying.\n{2}, {
- oracle_text advisory -- Sigarda's Aid: oracle_text diverges (similarity 0.14); scryfall='You may cast Aura and Equipment spells as though they had flash.\nWhenever an Equipment you control enters, yo
- oracle_text advisory -- Dwalin, Weaponmaster: oracle_text diverges (similarity 0.12); scryfall='First strike\nWhenever Dwalin enters or attacks, put a hone counter on each Equipment you control. (Each hone 
- oracle_text advisory -- Ardenn, Intrepid Archaeologist: oracle_text diverges (similarity 0.17); scryfall='At the beginning of combat on your turn, you may attach any number of Auras and Equipment you control to targe
- oracle_text advisory -- Umezawa's Jitte: oracle_text diverges (similarity 0.47); scryfall="Whenever equipped creature deals combat damage, put two charge counters on Umezawa's Jitte.\nRemove a charge c
- oracle_text advisory -- Kor Duelist: oracle_text diverges (similarity 0.49); scryfall='As long as this creature is equipped, it has double strike. (It deals both first-strike and regular combat dam
- oracle_text advisory -- Puresteel Paladin: oracle_text diverges (similarity 0.34); scryfall='Whenever an Equipment you control enters, you may draw a card.\nMetalcraft — Equipment you control have equip 
- oracle_text advisory -- Balan, Wandering Knight: oracle_text diverges (similarity 0.37); scryfall='First strike\nBalan has double strike as long as two or more Equipment are attached to it.\n{1}{W}: Attach all
- oracle_text advisory -- Armored Skyhunter: oracle_text diverges (similarity 0.49); scryfall='Flying\nWhenever this creature attacks, look at the top six cards of your library. You may put an Aura or Equi
- oracle_text advisory -- Kemba, Kha Regent: oracle_text diverges (similarity 0.34); scryfall='At the beginning of your upkeep, create a 2/2 white Cat creature token for each Equipment attached to Kemba.'
- oracle_text advisory -- Stoneforge Mystic: oracle_text diverges (similarity 0.44); scryfall='When this creature enters, you may search your library for an Equipment card, reveal it, put it into your hand
- oracle_text advisory -- Unexpectedly Absent: oracle_text diverges (similarity 0.28); scryfall="Put target nonland permanent into its owner's library just beneath the top X cards of that library."
- oracle_text advisory -- Boros Garrison: oracle_text diverges (similarity 0.34); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {R}{
- oracle_text advisory -- Elvish Archdruid: oracle_text diverges (similarity 0.38); scryfall='Other Elf creatures you control get +1/+1.\n{T}: Add {G} for each Elf you control.'
- oracle_text advisory -- Priest of Titania: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {G} for each Elf on the battlefield.'
- oracle_text advisory -- Arbor Elf: oracle_text diverges (similarity 0.12); scryfall='{T}: Untap target Forest.'
- oracle_text advisory -- Wirewood Lodge: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.\n{G}, {T}: Untap target Elf.'
- oracle_text advisory -- Worldly Tutor: oracle_text diverges (similarity 0.39); scryfall='Search your library for a creature card, reveal it, then shuffle and put the card on top.'
- oracle_text advisory -- Mirri's Guile: oracle_text diverges (similarity 0.44); scryfall='At the beginning of your upkeep, you may look at the top three cards of your library, then put them back in an
- oracle_text advisory -- Call of the Wild: oracle_text diverges (similarity 0.52); scryfall="{2}{G}{G}: Reveal the top card of your library. If it's a creature card, put it onto the battlefield. Otherwis
- oracle_text advisory -- Hornet Queen: oracle_text diverges (similarity 0.41); scryfall='Flying, deathtouch\nWhen this creature enters, create four 1/1 green Insect creature tokens with flying and de
- oracle_text advisory -- Terastodon: oracle_text diverges (similarity 0.21); scryfall='When this creature enters, you may destroy up to three target noncreature permanents. For each permanent put i
- oracle_text advisory -- Elderscale Wurm: oracle_text diverges (similarity 0.53); scryfall='Trample\nWhen this creature enters, if your life total is less than 7, your life total becomes 7.\nAs long as 
- oracle_text advisory -- Craterhoof Behemoth: oracle_text diverges (similarity 0.44); scryfall='Haste\nWhen this creature enters, creatures you control gain trample and get +X/+X until end of turn, where X 
- oracle_text advisory -- Worldspine Wurm: oracle_text diverges (similarity 0.39); scryfall="Trample\nWhen this creature dies, create three 5/5 green Wurm creature tokens with trample.\nWhen Worldspine W
- oracle_text advisory -- Vaultborn Tyrant: oracle_text diverges (similarity 0.47); scryfall="Trample\nWhenever this creature or another creature you control with power 4 or greater enters, you gain 3 lif
- oracle_text advisory -- Natural Order: oracle_text diverges (similarity 0.38); scryfall='As an additional cost to cast this spell, sacrifice a green creature.\nSearch your library for a green creatur
- oracle_text advisory -- Turntimber Symbiosis: oracle_text diverges (similarity 0.45); scryfall='Look at the top seven cards of your library. You may put a creature card from among them onto the battlefield.
- oracle_text advisory -- Boros Reckoner: oracle_text diverges (similarity 0.24); scryfall='Whenever this creature is dealt damage, it deals that much damage to any target.\n{R/W}: This creature gains f
- oracle_text advisory -- Burning-Fist Minotaur: oracle_text diverges (similarity 0.17); scryfall='First strike\n{1}{R}, Discard a card: This creature gets +2/+0 until end of turn.'
- oracle_text advisory -- Deathbellow Raider: oracle_text diverges (similarity 0.16); scryfall='This creature attacks each combat if able.\n{2}{B}: Regenerate this creature.'
- oracle_text advisory -- Fanatic of Mogis: oracle_text diverges (similarity 0.39); scryfall='When this creature enters, it deals damage to each opponent equal to your devotion to red. (Each {R} in the ma
- oracle_text advisory -- Gnarled Scarhide: oracle_text diverges (similarity 0.34); scryfall="Bestow {3}{B} (If you cast this card for its bestow cost, it's an Aura spell with enchant creature. It becomes
- oracle_text advisory -- Kragma Warcaller: oracle_text diverges (similarity 0.33); scryfall='Minotaur creatures you control have haste.\nWhenever a Minotaur you control attacks, it gets +2/+0 until end o
- oracle_text advisory -- Neheb, the Worthy: oracle_text diverges (similarity 0.35); scryfall='First strike\nOther Minotaurs you control have first strike.\nAs long as you have one or fewer cards in hand, 
- oracle_text advisory -- Rageblood Shaman: oracle_text diverges (similarity 0.35); scryfall='Trample\nOther Minotaur creatures you control get +1/+1 and have trample.'
- oracle_text advisory -- Ragemonger: oracle_text diverges (similarity 0.45); scryfall='Minotaur spells you cast cost {B}{R} less to cast. This effect reduces only the amount of colored mana you pay
- oracle_text advisory -- Rakdos Carnarium: oracle_text diverges (similarity 0.36); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {B}{
- oracle_text advisory -- Sethron, Hurloon General: oracle_text diverges (similarity 0.34); scryfall='Whenever Sethron or another nontoken Minotaur you control enters, create a 2/3 red Minotaur creature token.\n{
- oracle_text advisory -- Slaughter-Priest of Mogis: oracle_text diverges (similarity 0.28); scryfall='Whenever you sacrifice a permanent, this creature gets +2/+0 until end of turn.\n{2}, Sacrifice another creatu
- oracle_text advisory -- Atsushi, the Blazing Sky: oracle_text diverges (similarity 0.43); scryfall='Flying, trample\nWhen Atsushi dies, choose one —\n• Exile the top two cards of your library. Until the end of 
- oracle_text advisory -- Inferno of the Star Mounts: oracle_text diverges (similarity 0.35); scryfall="This spell can't be countered.\nFlying, haste\n{R}: Inferno of the Star Mounts gets +1/+0 until end of turn. W
- oracle_text advisory -- Dragon Tempest: oracle_text diverges (similarity 0.34); scryfall='Whenever a creature you control with flying enters, it gains haste until end of turn.\nWhenever a Dragon you c
- oracle_text advisory -- Urza's Incubator: oracle_text diverges (similarity 0.15); scryfall='As this artifact enters, choose a creature type.\nCreature spells of the chosen type cost {2} less to cast.'
- oracle_text advisory -- Mind Stone: oracle_text diverges (similarity 0.25); scryfall='{T}: Add {C}.\n{1}, {T}, Sacrifice this artifact: Draw a card.'
- oracle_text advisory -- Fire Diamond: oracle_text diverges (similarity 0.11); scryfall='This artifact enters tapped.\n{T}: Add {R}.'
- oracle_text advisory -- Dragonspeaker Shaman: oracle_text diverges (similarity 0.15); scryfall='Dragon spells you cast cost {2} less to cast.'
- oracle_text advisory -- Glorybringer: oracle_text diverges (similarity 0.46); scryfall="Flying, haste\nYou may exert this creature as it attacks. When you do, it deals 4 damage to target non-Dragon 
- oracle_text advisory -- Haven of the Spirit Dragon: oracle_text diverges (similarity 0.32); scryfall='{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana only to cast a Dragon creature spell.\n{2}, {T}
- oracle_text advisory -- Nest Invader: oracle_text diverges (similarity 0.27); scryfall='When this creature enters, create a 0/1 colorless Eldrazi Spawn creature token. It has "Sacrifice this token: 
- oracle_text advisory -- Young Pyromancer: oracle_text diverges (similarity 0.28); scryfall='Whenever you cast an instant or sorcery spell, create a 1/1 red Elemental creature token.'
- oracle_text advisory -- Undercellar Myconid: oracle_text diverges (similarity 0.39); scryfall='Whenever this creature enters or dies, create a 1/1 green Saproling creature token.\n{T}: Add one mana of any 
- oracle_text advisory -- Frontline Heroism: oracle_text diverges (similarity 0.39); scryfall='When this enchantment enters, create a 1/1 red Soldier creature token with haste.\nWhenever you cast a spell t
- oracle_text advisory -- Adarkar Wastes: oracle_text diverges (similarity 0.29); scryfall='{T}: Add {C}.\n{T}: Add {W} or {U}. This land deals 1 damage to you.'
- oracle_text advisory -- Caves of Koilos: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.\n{T}: Add {W} or {B}. This land deals 1 damage to you.'
- oracle_text advisory -- Yavimaya Coast: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.\n{T}: Add {G} or {U}. This land deals 1 damage to you.'
- oracle_text advisory -- Llanowar Wastes: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.\n{T}: Add {B} or {G}. This land deals 1 damage to you.'
- oracle_text advisory -- Conservatory: oracle_text diverges (similarity 0.64); scryfall='This land enters tapped.\n{T}: Add {G} or {W}.\n{4}, {T}: Investigate. (Create a Clue token. It\'s an artifact
- oracle_text advisory -- Shivan Gorge: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.\n{2}{R}, {T}: Shivan Gorge deals 1 damage to each opponent.'
- oracle_text advisory -- Mariposa Military Base: oracle_text diverges (similarity 0.26); scryfall='You may have this land enter tapped. If you do, you get two rad counters.\n{T}: Add {C}.\n{5}, {T}: Draw a car
- oracle_text advisory -- Eldrazi Displacer: oracle_text diverges (similarity 0.47); scryfall="Devoid (This card has no color.)\n{2}{C}: Exile another target creature, then return it to the battlefield tap
- oracle_text advisory -- Emiel the Blessed: oracle_text diverges (similarity 0.48); scryfall="{3}: Exile another target creature you control, then return it to the battlefield under its owner's control.\n
- oracle_text advisory -- Cloud of Faeries: oracle_text diverges (similarity 0.25); scryfall='Flying\nWhen this creature enters, untap up to two lands.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Peregrine Drake: oracle_text diverges (similarity 0.22); scryfall='Flying\nWhen this creature enters, untap up to five lands.'
- oracle_text advisory -- Wild Growth: oracle_text diverges (similarity 0.50); scryfall='Enchant land\nWhenever enchanted land is tapped for mana, its controller adds an additional {G}.'
- oracle_text advisory -- Cryptolith Rite: oracle_text diverges (similarity 0.05); scryfall='Creatures you control have "{T}: Add one mana of any color."'
- oracle_text advisory -- Overgrowth: oracle_text diverges (similarity 0.61); scryfall='Enchant land\nWhenever enchanted land is tapped for mana, its controller adds an additional {G}{G}.'
- oracle_text advisory -- Fertile Ground: oracle_text diverges (similarity 0.49); scryfall='Enchant land\nWhenever enchanted land is tapped for mana, its controller adds an additional one mana of any co
- oracle_text advisory -- Trace of Abundance: oracle_text diverges (similarity 0.34); scryfall="Enchant land\nEnchanted land has shroud. (It can't be the target of spells or abilities.)\nWhenever enchanted 
- oracle_text advisory -- Training Grounds: oracle_text diverges (similarity 0.49); scryfall="Activated abilities of creatures you control cost {2} less to activate. This effect can't reduce the mana in t
- oracle_text advisory -- Eladamri's Call: oracle_text diverges (similarity 0.61); scryfall='Search your library for a creature card, reveal that card, put it into your hand, then shuffle.'
- oracle_text advisory -- Stroke of Genius: oracle_text diverges (similarity 0.22); scryfall='Target player draws X cards.'
- oracle_text advisory -- Vexing Shusher: oracle_text diverges (similarity 0.05); scryfall="This spell can't be countered.\n{R/G}: Target spell can't be countered."
- oracle_text advisory -- Essence Depleter: oracle_text diverges (similarity 0.13); scryfall='Devoid (This card has no color.)\n{1}{C}: Target opponent loses 1 life and you gain 1 life. ({C} represents co
- oracle_text advisory -- Dimensional Infiltrator: oracle_text diverges (similarity 0.14); scryfall="Devoid (This card has no color.)\nFlash\nFlying\n{1}{C}: Target opponent exiles the top card of their library.
- oracle_text advisory -- Living Wish: oracle_text diverges (similarity 0.08); scryfall='You may reveal a creature or land card you own from outside the game and put it into your hand. Exile Living W
- oracle_text advisory -- Aether Hub: oracle_text diverges (similarity 0.08); scryfall='When this land enters, you get {E} (an energy counter).\n{T}: Add {C}.\n{T}, Pay {E}: Add one mana of any colo
- oracle_text advisory -- Maelstrom Wanderer: oracle_text diverges (similarity 0.34); scryfall='Creatures you control have haste.\nCascade, cascade (When you cast this spell, exile cards from the top of you
- oracle_text advisory -- Annoyed Altisaur: oracle_text diverges (similarity 0.50); scryfall='Reach, trample\nCascade (When you cast this spell, exile cards from the top of your library until you exile a 
- oracle_text advisory -- Sakashima's Protege: oracle_text diverges (similarity 0.28); scryfall='Flash\nCascade (When you cast this spell, exile cards from the top of your library until you exile a nonland c
- oracle_text advisory -- Boarding Party: oracle_text diverges (similarity 0.62); scryfall='Haste\nCascade (When you cast this spell, exile cards from the top of your library until you exile a nonland c
- oracle_text advisory -- Breaching Dragonstorm: oracle_text diverges (similarity 0.26); scryfall="When this enchantment enters, exile cards from the top of your library until you exile a nonland card. You may
- oracle_text advisory -- Call Forth the Tempest: oracle_text diverges (similarity 0.40); scryfall="Cascade, cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland
- oracle_text advisory -- Creative Technique: oracle_text diverges (similarity 0.33); scryfall='Demonstrate (When you cast this spell, you may copy it. If you do, choose an opponent to also copy it.)\nShuff
- oracle_text advisory -- Dwarven Ruins: oracle_text diverges (similarity 0.09); scryfall='This land enters tapped.\n{T}: Add {R}.\n{T}, Sacrifice this land: Add {R}{R}.'
- oracle_text advisory -- Svyelunite Temple: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {U}.\n{T}, Sacrifice this land: Add {U}{U}.'
- oracle_text advisory -- Hickory Woodlot: oracle_text diverges (similarity 0.24); scryfall='This land enters tapped with two depletion counters on it.\n{T}, Remove a depletion counter from this land: Ad
- oracle_text advisory -- Melira, Sylvok Outcast: oracle_text diverges (similarity 0.34); scryfall="You can't get poison counters.\nCreatures you control can't have -1/-1 counters put on them.\nCreatures your o
- oracle_text advisory -- Vizier of Remedies: oracle_text diverges (similarity 0.55); scryfall='If one or more -1/-1 counters would be put on a creature you control, that many -1/-1 counters minus one are p
- oracle_text advisory -- Kitchen Finks: oracle_text diverges (similarity 0.44); scryfall="When this creature enters, you gain 2 life.\nPersist (When this creature dies, if it had no -1/-1 counters on 
- oracle_text advisory -- Murderous Redcap: oracle_text diverges (similarity 0.51); scryfall="When this creature enters, it deals damage equal to its power to any target.\nPersist (When this creature dies
- oracle_text advisory -- Carrion Feeder: oracle_text diverges (similarity 0.28); scryfall="This creature can't block.\nSacrifice a creature: Put a +1/+1 counter on this creature."
- oracle_text advisory -- Bloodthrone Vampire: oracle_text diverges (similarity 0.26); scryfall='Sacrifice a creature: This creature gets +2/+2 until end of turn.'
- oracle_text advisory -- Recruiter of the Guard: oracle_text diverges (similarity 0.35); scryfall='When this creature enters, you may search your library for a creature card with toughness 2 or less, reveal it
- oracle_text advisory -- Ranger of Eos: oracle_text diverges (similarity 0.33); scryfall='When this creature enters, you may search your library for up to two creature cards with mana value 1 or less,
- oracle_text advisory -- Severance Priest: oracle_text diverges (similarity 0.40); scryfall="Deathtouch\nWhen this creature enters, target opponent reveals their hand. You may choose a nonland card from 
- oracle_text advisory -- Birthing Pod: oracle_text diverges (similarity 0.25); scryfall="({G/P} can be paid with either {G} or 2 life.)\n{1}{G/P}, {T}, Sacrifice a creature: Search your library for a
- oracle_text advisory -- Chord of Calling: oracle_text diverges (similarity 0.25); scryfall="Convoke (Your creatures can help cast this spell. Each creature you tap while casting this spell pays for {1} 
- oracle_text advisory -- Reveillark: oracle_text diverges (similarity 0.22); scryfall="Flying\nWhen this creature leaves the battlefield, return up to two target creature cards with power 2 or less
- oracle_text advisory -- Felidar Guardian: oracle_text diverges (similarity 0.18); scryfall="When this creature enters, you may exile another target permanent you control, then return that card to the ba
- oracle_text advisory -- Voice of Resurgence: oracle_text diverges (similarity 0.32); scryfall='Whenever an opponent casts a spell during your turn and when this creature dies, create a green and white Elem
- oracle_text advisory -- Scavenging Ooze: oracle_text diverges (similarity 0.23); scryfall='{G}: Exile target card from a graveyard. If it was a creature card, put a +1/+1 counter on this creature and y
- oracle_text advisory -- Ravenous Chupacabra: oracle_text diverges (similarity 0.18); scryfall='When this creature enters, destroy target creature an opponent controls.'
- oracle_text advisory -- Reclamation Sage: oracle_text diverges (similarity 0.16); scryfall='When this creature enters, you may destroy target artifact or enchantment.'
- oracle_text advisory -- Celes, Rune Knight: oracle_text diverges (similarity 0.25); scryfall='When Celes enters, discard any number of cards, then draw that many cards plus one.\nWhenever one or more othe
- oracle_text advisory -- Drifting Meadow: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.\n{T}: Add {W}.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Polluted Mire: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.\n{T}: Add {B}.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Smoldering Crater: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.\n{T}: Add {R}.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Blasted Landscape: oracle_text diverges (similarity 0.30); scryfall='{T}: Add {C}.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Fetid Pools: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {U} or {B}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Irrigated Farmland: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {W} or {U}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Canyon Slough: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {B} or {R}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Sheltered Thicket: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {R} or {G}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Scattered Groves: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {G} or {W}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Glittering Massif: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {R} or {W}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Festering Thicket: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {B} or {G}.)\nThis land enters tapped.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Capital City: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.\n{1}, {T}: Add one mana of any color.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Forsake the Worldly: oracle_text diverges (similarity 0.22); scryfall='Exile target artifact or enchantment.\nCycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Fluctuator: oracle_text diverges (similarity 0.17); scryfall='Cycling abilities you activate cost {2} less to activate.'
- oracle_text advisory -- Drannith Stinger: oracle_text diverges (similarity 0.26); scryfall='Whenever you cycle another card, this creature deals 1 damage to each opponent.\nCycling {1} ({1}, Discard thi
- oracle_text advisory -- Hollow One: oracle_text diverges (similarity 0.20); scryfall="This spell costs {2} less to cast for each card you've cycled or discarded this turn.\nCycling {2} ({2}, Disca
- oracle_text advisory -- Unearth: oracle_text diverges (similarity 0.24); scryfall='Return target creature card with mana value 3 or less from your graveyard to the battlefield.\nCycling {2} ({2
- oracle_text advisory -- Scrying Sheets: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.\n{1}{S}, {T}: Look at the top card of your library. If that card is snow, you may reveal it and 
- oracle_text advisory -- Skred: oracle_text diverges (similarity 0.23); scryfall='Skred deals damage to target creature equal to the number of snow permanents you control.'
- oracle_text advisory -- Coldsteel Heart: oracle_text diverges (similarity 0.11); scryfall='This artifact enters tapped.\nAs this artifact enters, choose a color.\n{T}: Add one mana of the chosen color.
- oracle_text advisory -- Abominable Treefolk: oracle_text diverges (similarity 0.35); scryfall="Trample\nAbominable Treefolk's power and toughness are each equal to the number of snow permanents you control
- oracle_text advisory -- Arcum's Astrolabe: oracle_text diverges (similarity 0.25); scryfall='({S} can be paid with one mana from a snow source.)\nWhen this artifact enters, draw a card.\n{1}, {T}: Add on
- oracle_text advisory -- Frost Augur: oracle_text diverges (similarity 0.39); scryfall="{S}, {T}: Look at the top card of your library. If it's a snow card, you may reveal it and put it into your ha
- oracle_text advisory -- Ice-Fang Coatl: oracle_text diverges (similarity 0.21); scryfall='Flash\nFlying\nWhen this creature enters, draw a card.\nThis creature has deathtouch as long as you control at
- oracle_text advisory -- Jorn, God of Winter: oracle_text diverges (similarity 0.10); scryfall='Whenever Jorn attacks, untap each snow permanent you control.'
- oracle_text advisory -- Kaldring, the Rimestaff: oracle_text diverges (similarity 0.17); scryfall='{T}: You may play target snow permanent card from your graveyard this turn. If you do, it enters tapped.'
- oracle_text advisory -- Marit Lage's Slumber: oracle_text diverges (similarity 0.35); scryfall="Whenever Marit Lage's Slumber or another snow permanent you control enters, scry 1.\nAt the beginning of your 
- oracle_text advisory -- Rimefeather Owl: oracle_text diverges (similarity 0.26); scryfall="Flying\nRimefeather Owl's power and toughness are each equal to the number of snow permanents on the battlefie
- oracle_text advisory -- Rimescale Dragon: oracle_text diverges (similarity 0.29); scryfall="Flying\n{2}{S}: Tap target creature and put an ice counter on it. ({S} can be paid with one mana from a snow s
- oracle_text advisory -- Soul's Attendant: oracle_text diverges (similarity 0.12); scryfall='Whenever another creature enters, you may gain 1 life.'
- oracle_text advisory -- Auriok Champion: oracle_text diverges (similarity 0.12); scryfall='Protection from black and from red\nWhenever another creature enters, you may gain 1 life.'
- oracle_text advisory -- Ocelot Pride: oracle_text diverges (similarity 0.13); scryfall="First strike, lifelink\nAscend (If you control ten or more permanents, you get the city's blessing for the res
- oracle_text advisory -- Serra Ascendant: oracle_text diverges (similarity 0.21); scryfall='Lifelink (Damage dealt by this creature also causes you to gain that much life.)\nAs long as you have 30 or mo
- oracle_text advisory -- Ajani's Pridemate: oracle_text diverges (similarity 0.09); scryfall='Whenever you gain life, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Voice of the Blessed: oracle_text diverges (similarity 0.26); scryfall='Whenever you gain life, put a +1/+1 counter on this creature.\nAs long as this creature has four or more +1/+1
- oracle_text advisory -- Daxos, Blessed by the Sun: oracle_text diverges (similarity 0.18); scryfall="Daxos's toughness is equal to your devotion to white. (Each {W} in the mana costs of permanents you control co
- oracle_text advisory -- Archangel of Thune: oracle_text diverges (similarity 0.24); scryfall='Flying\nLifelink (Damage dealt by this creature also causes you to gain that much life.)\nWhenever you gain li
- oracle_text advisory -- Heliod, Sun-Crowned: oracle_text diverges (similarity 0.13); scryfall="Indestructible\nAs long as your devotion to white is less than five, Heliod isn't a creature.\nWhenever you ga
- oracle_text advisory -- Ranger-Captain of Eos: oracle_text diverges (similarity 0.13); scryfall="When this creature enters, you may search your library for a creature card with mana value 1 or less, reveal i
- oracle_text advisory -- Ajani, Strength of the Pride: oracle_text diverges (similarity 0.27); scryfall='+1: You gain life equal to the number of creatures you control plus the number of planeswalkers you control.\n
- oracle_text advisory -- Apex Altisaur: oracle_text diverges (similarity 0.21); scryfall="When this creature enters, it fights up to one target creature you don't control.\nEnrage — Whenever this crea
- oracle_text advisory -- World War Hulk: oracle_text diverges (similarity 0.17); scryfall='(As this Saga enters and after your draw step, add a lore counter. Sacrifice after III.)\nI — The next red or 
- oracle_text advisory -- Ghalta, Stampede Tyrant: oracle_text diverges (similarity 0.17); scryfall='Trample\nWhen Ghalta enters, put any number of creature cards from your hand onto the battlefield.'
- oracle_text advisory -- Lyra Dawnbringer: oracle_text diverges (similarity 0.13); scryfall='Flying\nFirst strike (This creature deals combat damage before creatures without first strike.)\nLifelink (Dam
- oracle_text advisory -- Youthful Valkyrie: oracle_text diverges (similarity 0.08); scryfall='Flying\nWhenever another Angel you control enters, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Bishop of Wings: oracle_text diverges (similarity 0.11); scryfall='Whenever an Angel you control enters, you gain 4 life.\nWhenever an Angel you control dies, create a 1/1 white
- oracle_text advisory -- Righteous Valkyrie: oracle_text diverges (similarity 0.15); scryfall="Flying\nWhenever another Angel or Cleric you control enters, you gain life equal to that creature's toughness.
- oracle_text advisory -- Seraph Sanctuary: oracle_text diverges (similarity 0.13); scryfall='When this land enters, you gain 1 life.\nWhenever an Angel you control enters, you gain 1 life.\n{T}: Add {C}.
- oracle_text advisory -- Resplendent Angel: oracle_text diverges (similarity 0.11); scryfall='Flying\nAt the beginning of each end step, if you gained 5 or more life this turn, create a 4/4 white Angel cr
- oracle_text advisory -- Legion Angel: oracle_text diverges (similarity 0.07); scryfall='Flying\nWhen this creature enters, you may reveal a card you own named Legion Angel from outside the game and 
- oracle_text advisory -- Serra the Benevolent: oracle_text diverges (similarity 0.16); scryfall='+2: Creatures you control with flying get +1/+1 until end of turn.\n−3: Create a 4/4 white Angel creature toke
- oracle_text advisory -- Giada, Font of Hope: oracle_text diverges (similarity 0.13); scryfall='Flying, vigilance\nEach other Angel you control enters with an additional +1/+1 counter on it for each Angel y
- oracle_text advisory -- Saproling Burst: oracle_text diverges (similarity 0.13); scryfall='Fading 7 (This enchantment enters with seven fade counters on it. At the beginning of your upkeep, remove a fa
- oracle_text advisory -- Shroofus Sproutsire: oracle_text diverges (similarity 0.06); scryfall='Trample\nWhenever a Saproling you control deals combat damage to a player, create that many 1/1 green Saprolin
- oracle_text advisory -- Slimefoot, the Stowaway: oracle_text diverges (similarity 0.05); scryfall='Whenever a Saproling you control dies, Slimefoot deals 1 damage to each opponent and you gain 1 life.\n{4}: Cr
- oracle_text advisory -- Vitaspore Thallid: oracle_text diverges (similarity 0.11); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Deathspore Thallid: oracle_text diverges (similarity 0.10); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Sporecrown Thallid: oracle_text diverges (similarity 0.22); scryfall="Each other creature you control that's a Fungus or Saproling gets +1/+1."
- oracle_text advisory -- Tukatongue Thallid: oracle_text diverges (similarity 0.13); scryfall='When this creature dies, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- Yavimaya Sapherd: oracle_text diverges (similarity 0.06); scryfall='When this creature enters, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- Deathbloom Thallid: oracle_text diverges (similarity 0.05); scryfall='When this creature dies, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- The Mycotyrant: oracle_text diverges (similarity 0.18); scryfall='Trample\nThe Mycotyrant\'s power and toughness are each equal to the number of creatures you control that are 
- oracle_text advisory -- Simic Growth Chamber: oracle_text diverges (similarity 0.21); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {G}{
- oracle_text advisory -- Psychotrope Thallid: oracle_text diverges (similarity 0.22); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Concordant Crossroads: oracle_text diverges (similarity 0.01); scryfall='All creatures have haste.'
- oracle_text advisory -- Brightcap Badger: oracle_text diverges (similarity 0.12); scryfall='Each Fungus and Saproling you control has "{T}: Add {G}."\nAt the beginning of your end step, create a 1/1 gre
- oracle_text advisory -- Fungus Frolic: oracle_text diverges (similarity 0.10); scryfall='Create two 1/1 green Saproling creature tokens. (Then exile this card. You may cast the creature later from ex
- oracle_text advisory -- Spore Swarm: oracle_text diverges (similarity 0.06); scryfall='Create three 1/1 green Saproling creature tokens.'
- oracle_text advisory -- Saproling Migration: oracle_text diverges (similarity 0.15); scryfall='Kicker {4} (You may pay an additional {4} as you cast this spell.)\nCreate two 1/1 green Saproling creature to
- oracle_text advisory -- Doubling Season: oracle_text diverges (similarity 0.28); scryfall='If an effect would create one or more tokens under your control, it creates twice that many of those tokens in
- oracle_text advisory -- Parallel Lives: oracle_text diverges (similarity 0.13); scryfall='If an effect would create one or more tokens under your control, it creates twice that many of those tokens in
- oracle_text advisory -- Primal Vigor: oracle_text diverges (similarity 0.09); scryfall='If one or more tokens would be created, twice that many of those tokens are created instead.\nIf one or more +
- oracle_text advisory -- Beastmaster Ascension: oracle_text diverges (similarity 0.17); scryfall='Whenever a creature you control attacks, you may put a quest counter on this enchantment.\nAs long as this enc
- oracle_text advisory -- Thallid: oracle_text diverges (similarity 0.17); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Thallid Shell-Dweller: oracle_text diverges (similarity 0.13); scryfall='Defender\nAt the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters 
- oracle_text advisory -- Sporesower Thallid: oracle_text diverges (similarity 0.12); scryfall='At the beginning of your upkeep, put a spore counter on each Fungus you control.\nRemove three spore counters 
- oracle_text advisory -- Utopia Mycon: oracle_text diverges (similarity 0.12); scryfall='At the beginning of your upkeep, put a spore counter on this creature.\nRemove three spore counters from this 
- oracle_text advisory -- Mycoloth: oracle_text diverges (similarity 0.14); scryfall='Devour 2 (As this creature enters, you may sacrifice any number of creatures. It enters with twice that many +
- oracle_text advisory -- Lightstall Inquisitor: oracle_text diverges (similarity 0.17); scryfall='Vigilance\nWhen this creature enters, each opponent exiles a card from their hand and may play that card for a
- oracle_text advisory -- Lyra, Archangel of Dawn: oracle_text diverges (similarity 0.06); scryfall='Flying\nWhenever you gain life, put a +1/+1 counter on each Angel you control.'
- oracle_text advisory -- Sunrise Sovereign: oracle_text diverges (similarity 0.10); scryfall='Other Giant creatures you control get +2/+2 and have trample.'
- oracle_text advisory -- Stinkdrinker Daredevil: oracle_text diverges (similarity 0.11); scryfall='Giant spells you cast cost {2} less to cast.'
- oracle_text advisory -- Giant Harbinger: oracle_text diverges (similarity 0.14); scryfall='When this creature enters, you may search your library for a Giant card, reveal it, then shuffle and put that 
- oracle_text advisory -- Pyroclasm: oracle_text diverges (similarity 0.05); scryfall='Pyroclasm deals 2 damage to each creature.'
- oracle_text advisory -- Hamletback Goliath: oracle_text diverges (similarity 0.09); scryfall="Whenever another creature enters, you may put X +1/+1 counters on this creature, where X is that creature's po
- oracle_text advisory -- Borderland Behemoth: oracle_text diverges (similarity 0.06); scryfall='Trample\nThis creature gets +4/+4 for each other Giant you control.'
- oracle_text advisory -- Inferno Titan: oracle_text diverges (similarity 0.13); scryfall='{R}: This creature gets +1/+0 until end of turn.\nWhenever this creature enters or attacks, it deals 3 damage 
- oracle_text advisory -- Surtland Flinger: oracle_text diverges (similarity 0.18); scryfall="Whenever this creature attacks, you may sacrifice another creature. When you do, this creature deals damage eq
- oracle_text advisory -- Tectonic Giant: oracle_text diverges (similarity 0.16); scryfall='Whenever this creature attacks or becomes the target of a spell an opponent controls, choose one —\n• This cre
- oracle_text advisory -- Metallic Mimic: oracle_text diverges (similarity 0.18); scryfall='As this creature enters, choose a creature type.\nThis creature is the chosen type in addition to its other ty
- oracle_text advisory -- Adaptive Automaton: oracle_text diverges (similarity 0.23); scryfall='As this creature enters, choose a creature type.\nThis creature is the chosen type in addition to its other ty
- oracle_text advisory -- Corsair Captain: oracle_text diverges (similarity 0.18); scryfall='When this creature enters, create a Treasure token. (It\'s an artifact with "{T}, Sacrifice this token: Add on
- oracle_text advisory -- Dire Fleet Captain: oracle_text diverges (similarity 0.17); scryfall='Whenever this creature attacks, it gets +1/+1 until end of turn for each other attacking Pirate.'
- oracle_text advisory -- Goblin Tomb Raider: oracle_text diverges (similarity 0.09); scryfall='As long as you control an artifact, this creature gets +1/+0 and has haste.'
- oracle_text advisory -- Forerunner of the Coalition: oracle_text diverges (similarity 0.22); scryfall='When this creature enters, you may search your library for a Pirate card, reveal it, then shuffle and put that
- oracle_text advisory -- Staunch Crewmate: oracle_text diverges (similarity 0.33); scryfall='When this creature enters, look at the top four cards of your library. You may reveal an artifact or Pirate ca
- oracle_text advisory -- Malcolm, the Eyes: oracle_text diverges (similarity 0.26); scryfall='Flying, haste\nWhenever you cast your second spell each turn, investigate. (Create a Clue token. It\'s an arti
- oracle_text advisory -- Siren Stormtamer: oracle_text diverges (similarity 0.16); scryfall='Flying\n{U}, Sacrifice this creature: Counter target spell or ability that targets you or a creature you contr
- oracle_text advisory -- Daring Buccaneer: oracle_text diverges (similarity 0.09); scryfall='As an additional cost to cast this spell, reveal a Pirate card from your hand or pay {2}.'
- oracle_text advisory -- Kitesail Larcenist: oracle_text diverges (similarity 0.17); scryfall='Flying, ward {1}\nWhen this creature enters, for each player, choose up to one other target artifact or creatu
- oracle_text advisory -- Spirebluff Canal: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped unless you control two or fewer other lands.\n{T}: Add {U} or {R}.'
- oracle_text advisory -- Blackcleave Cliffs: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped unless you control two or fewer other lands.\n{T}: Add {B} or {R}.'
- oracle_text advisory -- Battlefield Forge: oracle_text diverges (similarity 0.18); scryfall='{T}: Add {C}.\n{T}: Add {R} or {W}. This land deals 1 damage to you.'
- oracle_text advisory -- Karplusan Forest: oracle_text diverges (similarity 0.43); scryfall='{T}: Add {C}.\n{T}: Add {R} or {G}. This land deals 1 damage to you.'
- oracle_text advisory -- Tarnished Citadel: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.\n{T}: Add one mana of any color. This land deals 3 damage to you.'
- oracle_text advisory -- Grand Coliseum: oracle_text diverges (similarity 0.53); scryfall='This land enters tapped.\n{T}: Add {C}.\n{T}: Add one mana of any color. This land deals 1 damage to you.'
- oracle_text advisory -- Ancient Tomb: oracle_text diverges (similarity 0.17); scryfall='{T}: Add {C}{C}. This land deals 2 damage to you.'
- oracle_text advisory -- Manabarbs: oracle_text diverges (similarity 0.12); scryfall='Whenever a player taps a land for mana, this enchantment deals 1 damage to that player.'
- oracle_text advisory -- Tamanoa: oracle_text diverges (similarity 0.13); scryfall='Whenever a noncreature source you control deals damage, you gain that much life.'
- oracle_text advisory -- Rhox Faithmender: oracle_text diverges (similarity 0.37); scryfall='Lifelink (Damage dealt by this creature also causes you to gain that much life.)\nIf you would gain life, you 
- oracle_text advisory -- Vito, Thorn of the Dusk Rose: oracle_text diverges (similarity 0.16); scryfall='Whenever you gain life, target opponent loses that much life.\n{3}{B}{B}: Creatures you control gain lifelink 
- oracle_text advisory -- Dina, Soul Steeper: oracle_text diverges (similarity 0.21); scryfall="Whenever you gain life, each opponent loses 1 life.\n{1}, Sacrifice another creature: Dina gets +X/+0 until en
- oracle_text advisory -- Bilbo, Birthday Celebrant: oracle_text diverges (similarity 0.24); scryfall='If you would gain life, you gain that much life plus 1 instead.\n{2}{W}{B}{G}, {T}, Exile Bilbo: Search your l
- oracle_text advisory -- Purity: oracle_text diverges (similarity 0.24); scryfall="Flying\nIf noncombat damage would be dealt to you, prevent that damage. You gain life equal to the damage prev
- oracle_text advisory -- Spellshock: oracle_text diverges (similarity 0.09); scryfall='Whenever a player casts a spell, this enchantment deals 2 damage to that player.'
- oracle_text advisory -- Pyrohemia: oracle_text diverges (similarity 0.11); scryfall='At the beginning of the end step, if no creatures are on the battlefield, sacrifice this enchantment.\n{R}: Th
- oracle_text advisory -- Rolling Earthquake: oracle_text diverges (similarity 0.08); scryfall='Rolling Earthquake deals X damage to each creature without horsemanship and each player.'
- oracle_text advisory -- Beseech the Queen: oracle_text diverges (similarity 0.20); scryfall="({2/B} can be paid with any two mana or with {B}. This card's mana value is 6.)\nSearch your library for a car
- oracle_text advisory -- Green Sun's Zenith: oracle_text diverges (similarity 0.17); scryfall="Search your library for a green creature card with mana value X or less, put it onto the battlefield, then shu
- oracle_text advisory -- Dimir House Guard: oracle_text diverges (similarity 0.25); scryfall="Fear (This creature can't be blocked except by artifact creatures and/or black creatures.)\nSacrifice a creatu
- oracle_text advisory -- Shriekmaw: oracle_text diverges (similarity 0.26); scryfall="Fear (This creature can't be blocked except by artifact creatures and/or black creatures.)\nWhen this creature
- oracle_text advisory -- Timeless Witness: oracle_text diverges (similarity 0.29); scryfall="When this creature enters, return target card from your graveyard to your hand.\nEternalize {5}{G}{G} ({5}{G}{
- oracle_text advisory -- Acidic Slime: oracle_text diverges (similarity 0.25); scryfall='Deathtouch (Any amount of damage this deals to a creature is enough to destroy it.)\nWhen this creature enters
- oracle_text advisory -- Ageless Entity: oracle_text diverges (similarity 0.03); scryfall='Whenever you gain life, put that many +1/+1 counters on this creature.'
- oracle_text advisory -- Selesnya Sanctuary: oracle_text diverges (similarity 0.06); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {G}{
- oracle_text advisory -- Blossoming Sands: oracle_text diverges (similarity 0.06); scryfall='This land enters tapped.\nWhen this land enters, you gain 1 life.\n{T}: Add {G} or {W}.'
- oracle_text advisory -- Verdant Sun's Avatar: oracle_text diverges (similarity 0.05); scryfall="Whenever this creature or another creature you control enters, you gain life equal to that creature's toughnes
- oracle_text advisory -- Feed the Clan: oracle_text diverges (similarity 0.04); scryfall='You gain 5 life.\nFerocious — You gain 10 life instead if you control a creature with power 4 or greater.'
- oracle_text advisory -- Accomplished Alchemist: oracle_text diverges (similarity 0.04); scryfall='{T}: Add one mana of any color.\n{T}: Add X mana of any one color, where X is the amount of life you gained th
- oracle_text advisory -- Blighted Steppe: oracle_text diverges (similarity 0.04); scryfall='{T}: Add {C}.\n{3}{W}, {T}, Sacrifice this land: You gain 2 life for each creature you control.'
- oracle_text advisory -- Wellwisher: oracle_text diverges (similarity 0.02); scryfall='{T}: You gain 1 life for each Elf on the battlefield.'
- oracle_text advisory -- Nykthos Paragon: oracle_text diverges (similarity 0.03); scryfall='Whenever you gain life, you may put that many +1/+1 counters on each creature you control. Do this only once e
- oracle_text advisory -- Blossoming Bogbeast: oracle_text diverges (similarity 0.05); scryfall='Whenever this creature attacks, you gain 2 life. Then creatures you control gain trample and get +X/+X until e
- oracle_text advisory -- Genesis Wave: oracle_text diverges (similarity 0.07); scryfall="Reveal the top X cards of your library. You may put any number of permanent cards with mana value X or less fr
- oracle_text advisory -- Champion of the Parish: oracle_text diverges (similarity 0.11); scryfall='Whenever another Human you control enters, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Field Marshal: oracle_text diverges (similarity 0.13); scryfall='Other Soldier creatures get +1/+1 and have first strike. (They deal combat damage before creatures without fir
- oracle_text advisory -- Thalia, Guardian of Thraben: oracle_text diverges (similarity 0.06); scryfall='First strike\nNoncreature spells cost {1} more to cast.'
- oracle_text advisory -- Thalia's Lieutenant: oracle_text diverges (similarity 0.12); scryfall='When this creature enters, put a +1/+1 counter on each other Human you control.\nWhenever another Human you co
- oracle_text advisory -- Rick, Steadfast Leader: oracle_text diverges (similarity 0.16); scryfall="As Greymond, Avacyn's Stalwart enters, choose two abilities from among first strike, vigilance, and lifelink.\
- oracle_text advisory -- Esper Sentinel: oracle_text diverges (similarity 0.14); scryfall="Whenever an opponent casts their first noncreature spell each turn, draw a card unless that player pays {X}, w
- oracle_text advisory -- Coppercoat Vanguard: oracle_text diverges (similarity 0.17); scryfall='Each other Human you control gets +1/+0 and has ward {1}. (Whenever it becomes the target of a spell or abilit
- oracle_text advisory -- Recruitment Officer: oracle_text diverges (similarity 0.17); scryfall='{3}{W}: Look at the top four cards of your library. You may reveal a creature card with mana value 3 or less f
- oracle_text advisory -- Jirina, Dauntless General: oracle_text diverges (similarity 0.09); scryfall="When Jirina enters, exile target player's graveyard.\nSacrifice Jirina: Humans you control gain hexproof and i
- oracle_text advisory -- Harbin, Vanguard Aviator: oracle_text diverges (similarity 0.08); scryfall='Flying\nWhenever you attack with five or more Soldiers, creatures you control get +1/+1 and gain flying until 
- oracle_text advisory -- General Kudro of Drannith: oracle_text diverges (similarity 0.19); scryfall="Other Humans you control get +1/+1.\nWhenever General Kudro or another Human you control enters, exile target 
- oracle_text advisory -- Cathar Commando: oracle_text diverges (similarity 0.06); scryfall='Flash\n{1}, Sacrifice this creature: Destroy target artifact or enchantment.'
- oracle_text advisory -- Brutal Cathar: oracle_text diverges (similarity 0.10); scryfall='Whenever this creature enters or transforms into Brutal Cathar, exile target creature an opponent controls unt
- oracle_text advisory -- Moonrage Brute: oracle_text diverges (similarity 0.25); scryfall='First strike\nWard—Pay 3 life.\nNightbound (If a player casts at least two spells during their own turn, it be
- oracle_text advisory -- King Darien XLVIII: oracle_text diverges (similarity 0.17); scryfall='Other creatures you control get +1/+1.\n{3}{G}{W}: Put a +1/+1 counter on King Darien and create a 1/1 white S
- oracle_text advisory -- Fortified Beachhead: oracle_text diverges (similarity 0.26); scryfall='As this land enters, you may reveal a Soldier card from your hand. This land enters tapped unless you revealed
- oracle_text advisory -- Silent Clearing: oracle_text diverges (similarity 0.10); scryfall='{T}, Pay 1 life: Add {W} or {B}.\n{1}, {T}, Sacrifice this land: Draw a card.'
- oracle_text advisory -- Adelbert Steiner: oracle_text diverges (similarity 0.02); scryfall='Lifelink\nAdelbert Steiner gets +1/+1 for each Equipment you control.'
- oracle_text advisory -- Adaptive Omnitool: oracle_text diverges (similarity 0.07); scryfall='Equipped creature gets +1/+1 for each artifact you control.\nWhenever equipped creature attacks, look at the t
- oracle_text advisory -- Kíli the Resourceful: oracle_text diverges (similarity 0.07); scryfall='Storied (If you control three or more artifacts, legendaries, and/or Sagas, you have an enduring story for the
- oracle_text advisory -- Almost Perfect: oracle_text diverges (similarity 0.20); scryfall='Enchant creature\nEnchanted creature has base power and toughness 9/10 and has indestructible.'
- oracle_text advisory -- Arcanum Wings: oracle_text diverges (similarity 0.15); scryfall='Enchant creature\nEnchanted creature has flying.\nAura swap {2}{U} ({2}{U}: Exchange this Aura with an Aura ca
- oracle_text advisory -- Boseiju, Who Endures: oracle_text diverges (similarity 0.38); scryfall='{T}: Add {G}.\nChannel — {1}{G}, Discard this card: Destroy target artifact, enchantment, or nonbasic land an 
- oracle_text advisory -- Botanical Sanctum: oracle_text diverges (similarity 0.29); scryfall='This land enters tapped unless you control two or fewer other lands.\n{T}: Add {G} or {U}.'
- oracle_text advisory -- Bruna, Light of Alabaster: oracle_text diverges (similarity 0.24); scryfall='Flying, vigilance\nWhenever Bruna attacks or blocks, you may attach to it any number of Auras on the battlefie
- oracle_text advisory -- Colossification: oracle_text diverges (similarity 0.14); scryfall='Enchant creature\nWhen this Aura enters, tap enchanted creature.\nEnchanted creature gets +20/+20.'
- oracle_text advisory -- Eldrazi Conscription: oracle_text diverges (similarity 0.39); scryfall='Enchant creature\nEnchanted creature gets +10/+10 and has trample and annihilator 2. (Whenever it attacks, def
- oracle_text advisory -- Glittering Wish: oracle_text diverges (similarity 0.06); scryfall='You may reveal a multicolored card you own from outside the game and put it into your hand. Exile Glittering W
- oracle_text advisory -- Indrik Umbra: oracle_text diverges (similarity 0.46); scryfall='Enchant creature\nEnchanted creature gets +4/+4 and has first strike, and all creatures able to block it do so
- oracle_text advisory -- Mother of Runes: oracle_text diverges (similarity 0.13); scryfall='{T}: Target creature you control gains protection from the color of your choice until end of turn.'
- oracle_text advisory -- Mythic Proportions: oracle_text diverges (similarity 0.40); scryfall='Enchant creature\nEnchanted creature gets +8/+8 and has trample.'
- oracle_text advisory -- Open the Armory: oracle_text diverges (similarity 0.21); scryfall='Search your library for an Aura or Equipment card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Prodigious Growth: oracle_text diverges (similarity 0.26); scryfall='Enchant creature\nEnchanted creature gets +7/+7 and has trample.'
- oracle_text advisory -- Seaside Citadel: oracle_text diverges (similarity 0.15); scryfall='This land enters tapped.\n{T}: Add {G}, {W}, or {U}.'
- oracle_text advisory -- Skycloud Expanse: oracle_text diverges (similarity 0.06); scryfall='{1}, {T}: Add {W}{U}.'
- oracle_text advisory -- Somberwald Sage: oracle_text diverges (similarity 0.12); scryfall='{T}: Add three mana of any one color. Spend this mana only to cast creature spells.'
- oracle_text advisory -- Unflinching Courage: oracle_text diverges (similarity 0.43); scryfall='Enchant creature\nEnchanted creature gets +2/+2 and has trample and lifelink. (Damage dealt by the creature al
- oracle_text advisory -- Linvala, Shield of Sea Gate: oracle_text diverges (similarity 0.31); scryfall="Flying\nAt the beginning of combat on your turn, if you have a full party, choose target nonland permanent an 
- oracle_text advisory -- Troyan, Gutsy Explorer: oracle_text diverges (similarity 0.15); scryfall='{T}: Add {G}{U}. Spend this mana only to cast spells with mana value 5 or greater or spells with {X} in their 
- oracle_text advisory -- Detention Sphere: oracle_text diverges (similarity 0.25); scryfall="When this enchantment enters, you may exile target nonland permanent not named Detention Sphere and all other 
- oracle_text advisory -- Steel of the Godhead: oracle_text diverges (similarity 0.31); scryfall="Enchant creature\nAs long as enchanted creature is white, it gets +1/+1 and has lifelink. (Damage dealt by the
- oracle_text advisory -- Auroral Procession: oracle_text diverges (similarity 0.07); scryfall='Return target card from your graveyard to your hand.'
- oracle_text advisory -- Reborn Hope: oracle_text diverges (similarity 0.10); scryfall='Return target multicolored card from your graveyard to your hand.'
- oracle_text advisory -- Rampaging Baloths: oracle_text diverges (similarity 0.14); scryfall='Trample\nLandfall — Whenever a land you control enters, create a 4/4 green Beast creature token.'
- oracle_text advisory -- Avenger of Zendikar: oracle_text diverges (similarity 0.21); scryfall='When this creature enters, create a 0/1 green Plant creature token for each land you control.\nLandfall — When
- oracle_text advisory -- Omnath, Locus of Rage: oracle_text diverges (similarity 0.20); scryfall='Landfall — Whenever a land you control enters, create a 5/5 red and green Elemental creature token.\nWhenever 
- oracle_text advisory -- Phylath, World Sculptor: oracle_text diverges (similarity 0.19); scryfall='When Phylath enters, create a 0/1 green Plant creature token for each basic land you control.\nLandfall — When
- oracle_text advisory -- Courser of Kruphix: oracle_text diverges (similarity 0.17); scryfall='Play with the top card of your library revealed.\nYou may play lands from the top of your library.\nLandfall —
- oracle_text advisory -- Khalni Heart Expedition: oracle_text diverges (similarity 0.19); scryfall='Landfall — Whenever a land you control enters, you may put a quest counter on this enchantment.\nRemove three 
- oracle_text advisory -- Rampant Growth: oracle_text diverges (similarity 0.16); scryfall='Search your library for a basic land card, put that card onto the battlefield tapped, then shuffle.'
- oracle_text advisory -- Search for Tomorrow: oracle_text diverges (similarity 0.27); scryfall='Search your library for a basic land card, put it onto the battlefield, then shuffle.\nSuspend 2—{G} (Rather t
- oracle_text advisory -- Cultivate: oracle_text diverges (similarity 0.24); scryfall='Search your library for up to two basic land cards, reveal those cards, put one onto the battlefield tapped an
- oracle_text advisory -- Evolving Wilds: oracle_text diverges (similarity 0.11); scryfall='{T}, Sacrifice this land: Search your library for a basic land card, put it onto the battlefield tapped, then 
- oracle_text advisory -- Terramorphic Expanse: oracle_text diverges (similarity 0.11); scryfall='{T}, Sacrifice this land: Search your library for a basic land card, put it onto the battlefield tapped, then 
- oracle_text advisory -- Myriad Landscape: oracle_text diverges (similarity 0.25); scryfall='This land enters tapped.\n{T}: Add {C}.\n{2}, {T}, Sacrifice this land: Search your library for up to two basi
- oracle_text advisory -- Primeval Titan: oracle_text diverges (similarity 0.16); scryfall='Trample\nWhenever this creature enters or attacks, you may search your library for up to two land cards, put t
- oracle_text advisory -- Radiant Fountain: oracle_text diverges (similarity 0.12); scryfall='When this land enters, you gain 2 life.\n{T}: Add {C}.'
- oracle_text advisory -- Haunted Fengraf: oracle_text diverges (similarity 0.12); scryfall='{T}: Add {C}.\n{3}, {T}, Sacrifice this land: Return a creature card at random from your graveyard to your han
- clause_ledger: no dedicated per-clause artifact. Its function -- every oracle clause modeled/inert/deferred -- is covered by coverage(partial hard-stop) + bracket-note deferrals + viewer oracle cross-check + audit_card_fields oracle-diff. A dedicated ledger is deferred (high per-card cost, marginal added rigor).
- claude_sweep recorded at commit 223cbba4 (HEAD e05b38c28d05); re-run if play changed since (NOTE: Landfall is NOT a regression case, so NO digest tracks its play -- nothing will tell you when this record goes stale. Re-run the sweep on judgement, or add the deck to the suite).

<!-- verify_deck:end -->
