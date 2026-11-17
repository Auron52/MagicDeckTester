# Maze's End deck analysis

Status: IN PROGRESS (started 2026-11-17). Stage 1 done; Stage 2 (implementation) starting.

## Deck

Source list: `decks/Maze's End/Maze's End.cod` (60 cards, no sideboard). Win condition: Maze's End's
activated ability ("{3}, {T}, Return Maze's End to its owner's hand: Search your library for a Gate
card, put it onto the battlefield, then shuffle. If you control ten or more Gates with different
names, you win the game."). There is no damage-based win in the list, so every win is an
alternate win.

Engines: Exploration (extra land drop), Burgeoning (land from hand on each opponent land play;
uses the existing passive-opponent land model, `ResolvePassiveOpponentLandTurn`), Amulet of Vigor
(untaps tapped-entering permanents, so Gates and Maze's End enter untapped), Manabond (end-step
dump of every land in hand, discard the rest), Guild Summit (draw per Gate entering), Sylvan
Scrying / Archdruid's Charm (land tutors).

## Stage 1 -- coverage (2026-11-17)

32 of 33 names missing; only Sylvan Scrying was in `cards.json`. No fetchlands, no sideboard.
Missing: Maze's End, the ten Guildgates, Exploration, Burgeoning, Amulet of Vigor, Manabond, Guild
Summit, Thran Portal, Sea Gate, Manor Gate, Heap Gate, Gond Gate, Cliffgate, Black Dragon Gate,
Baldur's Gate, Citadel Gate, Basilisk Gate, Gates Ablaze, Gateway Plaza, Archdruid's Charm,
Planar Nexus, Talon Gates of Madara, The Black Gate.

Oracle text for all 32 was fetched from Scryfall into `build/scratch/maze/oracle.jsonl`
(regenerable; `build/scratch/maze/fetch.sh`).

## Stage 2 -- implementation

Work happens on branch `wip/maze-end` in the worktree `.claude/worktrees/maze-end` (based on
`origin/phase-1-2-deck-analyzer` at `18ab35ca8`). Research drafts, one per card family, with file:line
anchors (gitignored, on the host drive): `logs/maze/draft-win-tutor.md`,
`logs/maze/draft-core-gates.md`, `logs/maze/draft-utility-gates.md`,
`logs/maze/draft-land-engines.md` (all under `/workspaces/MagicDeckTester/logs/maze/`).
The oracle text in the drafts is reconstructed from XMage, because Scryfall was rate-limiting this
IP. Every entry is re-pasted from Scryfall and passes `audit_card_costs.py` /
`audit_card_fields.py` before it counts as done.

Integration phases (serial, one integrator agent each, handoff file between them):

| Phase | Scope | Status |
|---|---|---|
| I1 | Gate infrastructure (Planar Nexus nonbasic-types bit, `IsGate`, `CountDistinctNamedGates`, name-distinct land folds), `won_by_effect` alternate win, Maze's End (generalised TapTutorPut + return-self cost + win check), 10 Guildgates, Planar Nexus, Guild Summit, Gates Ablaze, Archdruid's Charm, the deck's own `MazesEndProvider` (tutor ordering/width, TapTutorPut win fold, `ProjectsAlternateWin`) | DONE `187054fba` (wip/maze-end); unit suite 3402/3402; smoke attribution in progress (see below) |
| I2 | Land-entry pipeline: controller-aware enters-tapped predicate, Gond Gate, Amulet of Vigor (single + multi-copy float), Thran Portal, Gateway Plaza, the five chosen-colour CLB Gates + `land_color` axis, The Black Gate | pending |
| I3 | Exploration (static drops + multi-drop search route), Burgeoning (opponent-turn crossing fan), Manabond (end-step axis) | pending |
| I4 | Heap Gate, Baldur's Gate, Basilisk Gate, Talon Gates of Madara | pending |

### I1 cards (2026-11-17)

| Card | Tier | Params | Viewer bucket |
|---|---|---|---|
| Maze's End | 2 | `produces [C]`, `enters_tapped`, `tap_tutor_put_cost {3}`, `tap_tutor_put_return_self`, `tutor_types [Land]`, `tutor_required_subtype Gate`, `tutor_shuffle_after`, `alt_win_distinct_named_subtype Gate`, `alt_win_distinct_named_count 10` | A: `main_phase` TapTutorPut variants (one per Gate name + fail-to-find; human play unpruned) |
| Azorius / Boros / Dimir / Golgari / Gruul / Izzet / Orzhov / Rakdos / Selesnya / Simic Guildgate | 1 | `produces [two colours]`, `enters_tapped` (subtype Gate) | none (colour = mana-payment exemption) |
| Planar Nexus | 2 | `produces WUBRG`, `any_color_filter`, `all_nonbasic_land_types` | none |
| Guild Summit | 2 | `etb_tap_subtype_draw Gate`, `draw_on_other_subtype_enter [Gate]` | A+: `x_value` (new kind `etb_tap_draw`) for the count, then `target` (loyalty chooser) per Gate |
| Gates Ablaze | 2 | `damage_all_creatures_x_per_subtype Gate` | none (no choice; provider `NeverCast` autonomously) |
| Archdruid's Charm | 2 | `tutor_land_to_battlefield`, `tutor_types [Creature, Land]`, `tutor_land_enters_tapped`, `tutor_nonland_to_hand`, `tutor_shuffle_after`, `modal_exile_target_artifact_enchantment` | A: `main_phase` tutor axis (mode 1); `main_phase` cast variants per legal target, pin `AX<n>` (mode 3, human play only); mode 2 not modelled (PROVISIONAL, decision 9) |

Shared infrastructure landed in I1: `SubtypeSet` every-nonbasic-land-type bit (Planar Nexus; fallback in
`HasId` / `CardHasSubtype` / `LandHasSubtypeNow`, never a basic type); `IsGate`;
`CountDistinctNamedLandsWithSubtype` / `CountDistinctNamedGates`; Gate names kept in `behaviour_identity`
and in the land-drop signature (`land_sig_named`); `GameState::won_by_effect` (in `OpponentHasLost`,
`dmgev::OpponentLostRaw`, `dominance::Build`, `BuildSimKeyBase`, JSON `alt_win`); def-driven
`TapTutorPutLibraryCandidate` (Seahunter identical, unit-tested over every card in the DB);
`ReturnPermanentToOwnersHand` (Oboro's SelfBounce now shares it); a LAND put by
`PerformTutorToBattlefield` enters through `EnterLand`; the subtype-enter draw watcher
(`FireOtherSubtypeEnterDraws`) fires for a land at `OnLandEntered` (once per land, every route) and for a
nonland at `FireEtbWatchers`; `MazesEndProvider` (routed first).
`LandDefsShareLandType` honours Planar Nexus (shares a type with any land that has a nonbasic land type,
never a basic). Archdruid's Charm mode 3 is a cast variant per legal artifact/enchantment (pin `AX<n>`,
`#AX` in both plan signatures, no tutor variants on it, viewer label "(exile <name> #n)"), offered in human
play; `MazesEndProvider::OffersModalExileArtEnch` withholds it autonomously.

Oracle text and mana cost of every I1 card match live Scryfall data verbatim
(`build/scratch/maze/oracle.jsonl`, pulled 2026-11-17); Guild Summit's self-reference now reads "this
enchantment".

I1 crash root cause (2026-11-17, I1b): the unit-test SIGSEGV / `vector larger than max_size()` crashes in
`EnumerateMainPlans` were STALE OBJECTS, not a code defect. `DecisionProvider.h` / `DecisionProviders.h`
were edited while the Release build was compiling `TurnSolver.cpp` / `DecisionProviders.cpp`; those
objects preprocessed the old headers but got mtimes newer than the edit, so ninja never rebuilt them, and
the two new virtuals inserted MID-class shifted every later vtable slot (a call from `EtbDigAxisWidth`
landed in `CombatCheatCandidates`). Fix: the two Guild Summit hooks moved to the END of `DecisionProvider`
(the class's own rule -- existing slots keep their offsets), every edited file touched, one clean build.
Lesson: never edit a header while `./build.sh` is running.

TapTutorPut target signature (I1b): neither plan signature carried a TapTutorPut activation's TARGET, so
Maze's End's per-Gate variants and its fail-to-find all folded onto the FIRST library name (the d3 unit test
won a turn late because the only surviving plan fetched a duplicate Gate). Fixed with a `#F<target>` tag in
both builders (`TapTutorPutTargetTagged`), gated on `tap_tutor_put_return_self` so Seahunter's signature is
unchanged. **OPEN QUESTION (USER): Seahunter has the same hole** -- when `MerfolkTurnsProvider::
SeahunterCandidates` does not narrow (Prophets out, a cast-then-tutor line, Sovereign lethal), every
Seahunter target but the first library name is folded away, an unsearched pick inside the search window.
Tagging it too would move Merfolk Turns play, so it is left for the user to schedule (it needs a Merfolk
A/B and a GT accept).

Viewer audit (I1b): `draw_on_other_subtype_enter` (Kili's and Guild Summit's mandatory draw trigger) was
unclassified; classified INERT (no 'may', no target). Sign-off collected, not blocking.

## Decisions and provisional deferrals (USER sign-off pending)

Defaults taken so the work can proceed (raised to the user; none blocks anything):

1. **Chosen colours (Thran Portal, five CLB Gates) are SEARCHED**, on a land-entry colour axis.
   The provider restricts the menu to colours some card in the list can spend (G/U/R here), which
   is sound dominance. The alternative is a Coldsteel-Heart-style provider pick.
2. **Guild Summit "tap any number of Gates":** the count N is searched; WHICH Gates are tapped
   follows the mana-source rank (least valuable first, protecting Gates with non-mana {T}
   abilities). This rests on the mana-payment exemption. The alternative is a searched tap set,
   about 2^U wide.
3. **Basilisk Gate's pump** and **Archdruid's Charm mode 3** are implemented, not deferred, though
   both are goldfish-inert. The provider never offers Charm mode 3 or Gates Ablaze autonomously
   (deck-scoped dominance).
4. **Gateway Plaza's pay-or-sacrifice** reuses the `echo` decision shape in the viewer.
5. **Gond Gate also untaps Gates "put onto the battlefield tapped"** (controller orders the
   replacements, CR 616.1), pending the Gatherer ruling.
6. **Burgeoning** turns on the existing passive-opponent land model (50-deck pool, the Mill
   model). Its trigger frequency is a property of that model, disclosed in 6a.
7. **Sylvan Scrying / Archdruid's Charm tutor width** covers every distinct land name (today's
   default width 6 over shuffle order is a random prune), ordered by the provider.
8. **Guild Summit's UNPINNED count** (a rollout leaf beyond the horizon, or a put rather than a cast) is
   ALL untapped Gates (`EtbTapSubtypeDrawDefault`). Inside the search window the count is searched.
9. **Archdruid's Charm mode 2 is NOT modelled** (PROVISIONAL deferral, USER sign-off pending): it needs a
   target creature we control, the list has none and makes none, so it can never be chosen (CR 601.2c).
   The card's bracket note says so.
10. **Guild Summit's which-Gates order is ManaSourceRank ascending** (decision 2). Protecting Gates with
   non-mana {T} abilities is NOT yet implemented: I4 must add it in `MazesEndProvider::EtbTapSubtypeOrder`.
11. **A land that has already left before `OnLandEntered` runs** (a self-bouncing Karoo) draws nothing off
   Guild Summit. No Gate does that.
12. **`TutorSearchWidth` 64** for the whole deck (coverage, not a heuristic).
13. **Archdruid's Charm mode 3 is offered only in human play** (and `DecisionUnpruned`); autonomously the
   provider withholds it (every legal target is our own permanent). Executor and rollout share one
   resolver.

## Approved deferrals

(none)

## Verification

(Stage 5 verdicts)
