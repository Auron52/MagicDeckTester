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
| Khalni Heart Expedition | 3 | landfall quest counter done (078acb9e); activation in progress (land-search integrator). | "you may" counter always taken (only feeds an optional activation) — PROVISIONAL |
| Genesis Wave (existing) | — | landfall in pass 2; CR 603.3b dominance trigger order (landfall decks only); left-land still fires; duplicate legend now PUT. | — |
| Mountain / Island (existing) | — | Basic supertype added; `audit_card_fields.py` hard-checks Basic. | — |

Commit `078acb9e` (2026-10-15 ~15:00): unit 616/616, scenarios 165/165, viewer linebuild 0 fail over
467 refs, smoke `play-changed=0` at every depth, ALL PASS. Contract + deferred items:
`docs/design/landfall-trigger-infrastructure.md`. Landfall currently routes to SelesnyaLifegainProvider
(keyed on Genesis Wave, an empty derivation) — Stage 4a gives it its own provider.

Remaining land-timing gaps (handed to the Courser integrator, 2026-10-15 ~15:05): a land in the MIDDLE
of the cast order (cast Khalni, play the land, cast Rampant Growth with its mana) and land-timing
inside breakpoint continuations.

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

## Open questions for the user (collected; work continues on the stated default)
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
