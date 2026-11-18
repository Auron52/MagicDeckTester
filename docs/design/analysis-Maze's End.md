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
| I1 | Gate infrastructure (Planar Nexus nonbasic-types bit, `IsGate`, `CountDistinctNamedGates`, name-distinct land folds), `won_by_effect` alternate win, Maze's End (generalised TapTutorPut + return-self cost + win check), 10 Guildgates, Planar Nexus, Guild Summit, Gates Ablaze, Archdruid's Charm, the deck's own `MazesEndProvider` (tutor ordering/width, TapTutorPut win fold, `ProjectsAlternateWin`) | DONE `187054fba` (wip/maze-end); unit suite 3402/3402; smoke byte-identical to the base (0 of 219 case digests differ) |
| I2 | Land-entry pipeline: controller-aware enters-tapped predicate, Gond Gate, Amulet of Vigor (single + multi-copy float), Thran Portal, Gateway Plaza, the five chosen-colour CLB Gates + the land-entry pin axis, The Black Gate | DONE `c71d96361` + `699e1137a` (wip/maze-end); unit suite 3411/3411; smoke byte-identical to I1 (all 219 cases' per-game digests) |
| I3 | Exploration (static drops + multi-drop search route), Burgeoning (opponent-turn crossing fan), Manabond (end-step axis) | DONE `4b416123c` + `ac59aafae` (I3b) (wip/maze-end); unit suite 3418/3418; in-window crossing census 0 searched-frame / 0 overflow over 90 games; smoke byte-identical to I2 (0 of 219 case digests differ; the 44 stale-GT failures are the base's) |
| I4 | Heap Gate, Baldur's Gate, Basilisk Gate, Talon Gates of Madara, the Gate tap order (Guild Summit / Heap's cost) | DONE `f63a6f138` + `61fd80c65` (I4b) (wip/maze-end); unit suite 3425/3425; in-window crossing census 0 searched-frame / 0 overflow over 90 games (after the I4b last-turn overrun guard); smoke byte-identical to I3b (0 of 219 case digests differ; the 44 stale-GT failures are the base's) |

**Stage 2 COMPLETE (2026-11-17).** Every card in the list is implemented (I1-I4; `--coverage-only` 33/33 full, 0 gaps). Open for the next stages: the deck has no `.profile.json` yet (provider routing / `provider_audit` "NOT AUDITED"), it is not in `test/regression_cases.sh`, and its cost (I4b: ~109 s/game d3 b10) must pass `suite_gate.py --cost` before either generator; decisions 2-33 await USER sign-off.

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

Smoke (I1b, 2026-11-17): the I1 binary reports 175 passed / 44 failed against committed GT, and so does
the BASE binary (`18ab35ca8`, built separately, same tree, same flags): all 219 case digests are identical
between the two, so I1 changes no other deck's play. The 44 failures are GT staleness already present at
the base (src commits after the last smoke accept, 09:03 UTC) -- not ours to accept. No GT accepted.

Viewer audit (I1b): `draw_on_other_subtype_enter` (Kili's and Guild Summit's mandatory draw trigger) was
unclassified; classified INERT (no 'may', no target). Sign-off collected, not blocking.

### I2 cards (2026-11-17)

| Card | Tier | Params | Viewer bucket |
|---|---|---|---|
| Gond Gate | 2 | `produces [C]`, `gates_enter_untapped`, `mana_colors_of_controlled_subtype Gate` | none (static replacement; colour per tap = mana-payment exemption) |
| Amulet of Vigor | 2 (single) + 3 (multi-copy float) | `untap_own_permanent_entering_tapped` | none (mandatory trigger; order and float are sound dominances) |
| Thran Portal | 2 | `produces WUBRG` (menu), `fastland_max_other_lands 2`, `etb_choose_color`, `chosen_color_adds_basic_land_type`, `tap_self_damage 1`, `tap_is_life_payment` | A: `choose_color` at entry (basic land type = colour) |
| Gateway Plaza | 2 | `produces WUBRG`, `enters_tapped`, `etb_sacrifice_unless_pay {1}` | A: `dig` (pay = 0 / sacrifice = -1), only when affordable |
| Sea Gate / Manor Gate / Cliffgate / Citadel Gate / Black Dragon Gate | 2 | `produces [printed + other four]`, `enters_tapped`, `etb_choose_color`, `etb_choose_color_fixed <U/G/R/W/B>` | A: `choose_color` at entry (menu excludes the printed colour) |
| The Black Gate | 1 | Legendary; `produces [B]`, `etb_pay_life_to_untap 3`, `tap_target_pump_cost {1}{B}`, `tap_target_pump_grants_unblockable` | A: `land_entry` (pay 3 life), not asked under Gond Gate |

Oracle text and mana cost of every I2 card are pasted verbatim from `build/scratch/maze/oracle.jsonl`
(Scryfall, 2026-11-17), with the modelling commentary in the bracket note.

Shared infrastructure landed in I2 (the LAND-ENTRY PIPELINE, `core/SpellEffects.h`):
- **Controller-aware predicate.** `LandWouldEnterTapped` / `LandEntryHasChoice` / `LandEntersTapped` /
  `ApplyLandUntapPayment` / `LandCanReveal` / `LandControlUntapMet` take the entering land's controller
  (`LandEntryController`, default = the active player, so every existing caller is byte-identical); `EnterLand`
  takes a trailing `controller` and creates the permanent under it. I3's Burgeoning passes the controller.
- **Gond Gate** is the predicate's FIRST branch (`GateEntersUntappedByGond`, a Gate test then a battlefield scan;
  non-Gates pay one id compare), so no shock life is paid for a Gate under it; it also overrides a
  `LandEntryTap::ForcedTapped` put (decision 5). Its mana is The Grey Havens' dynamic-colour shape
  (`ControlledSubtypeColors`, wired at every Havens site).
- **One post-entry sequence** for the drop (`PlayLandCore`) and every put (`EnterLand`): (1) as-enters
  replacements (tapped-ness, `LockEtbChosenColor`), (2) Amulet of Vigor (`ResolveEnteredTappedUntaps`; also from
  `FireEtbWatchers` for a nonland), (3) Gateway Plaza (`ResolveEtbSacUnlessPay`), (4) the other enter triggers
  and landfall. `LandUsableOnEntry` (= not tapped, or an Amulet will untap it) replaces the "adds mana now"
  projections (the land-draw breakpoint pre-filter, the greedy land picker).
- **The land-entry pin.** One int whose meaning is the land's (a colour ordinal; Plaza 1 = pay / 0 = sacrifice;
  Mariposa's rad mode), carried on `Plan::rad_mode` / `LandPlayOptions::rad_mode` for a land drop (every land
  route already threads it) and on `g_land_entry_pin` for an effect put (Maze's End's fetch: `chosen_x` = pin+1,
  installed by both worlds' apply). The land enumeration offers ONE LAND OPTION PER PIN
  (`add_for_land(..., entry_pin)`), so the casts are enumerated on the board that choice makes (an
  Amulet-untapped Cliffgate that chose G can cast Sylvan Scrying; one that chose U cannot) -- re-pinning a plan
  enumerated once would leave the other choice's lines unfindable. `DecisionProvider::LandEntryPinCandidates`
  (appended at the END of the class) supplies the values; `MazesEndProvider` restricts colours to those the
  list spends (decision 1). Plan label: `[color=X]` / `[pay]` / `[sacrifice]`.

Verification (I2b, 2026-11-17): `./build.sh` Release clean (two transient 9p `Cannot allocate memory` reruns, no
code errors); I2 tests compiled after two fixture fixes (`GenericProvider` for the base menu -- `DeckProvider` is
abstract; Sylvan Scrying stands in for Exploration, which is I3's card) and pass; new test for decision 15.
Unit suite 3411/3411. `provider_audit.py --check` exit 0 (Maze's End "NOT AUDITED": no profile until I4).
`audit_viewer_decisions.py --no-sweep` exit 0: expected decision types choose_color / dig / land_entry / target /
x_value; the oracle cross-check flags only Maze's End's search/bounce and the Charm's modal/target, all with
their disclosed notes. Smoke (worktree, Release): 175 passed / 44 failed -- the same 44 stale-GT cases as the base
and I1; the per-game `.wins` of all 219 cases (outcomes + play digests) are byte-identical to I1's
(`build/scratch/maze/smoke-i1/wins` vs `smoke-i2b/wins` on the primary build volume). No GT accepted.

Rules review of the I2 code (I2b, against `.claude/skills/mtg-rules.md` and the CR): no defect found.
- Gond Gate (CR 614.12 / 616.1): the predicate reads the board BEFORE the entry, so a Gond entering does not
  untap itself or a land entering with it (simultaneous multi-puts are I3's, note below); a Gate with its own
  "enters tapped" or The Black Gate's pay-3 replacement is untapped with no life paid -- the controller orders
  the replacements (enters-tapped first, Gond last). Planar Nexus counts as a Gate (the `HasId` fallback), for
  the untap and for Gond's colours (WUBRG). A second Gond's reflected mode is skipped (Reflecting Pool rule).
- Amulet of Vigor (CR 603.2 / 603.6a): one trigger per Amulet the ENTERING permanent's controller controls,
  counted after the entry, so an Amulet entering tapped sees itself; no trigger for a permanent that entered
  untapped (Gond). N copies float N-1 mana by tapping in response, main phase only.
- Gateway Plaza (CR 603.3b): Amulet's trigger resolves before the Plaza's, the controller's ordering and a
  sound dominance (it can then pay with the Plaza itself); payment through the shared payer; the affordability
  probe is the Mihail precedent's copy probe.
- Chosen colour / basic land type: locked as the land enters (a replacement, CR 614.12), before Amulet and the
  enter triggers; a CLB Gate's menu excludes its printed colour; Thran Portal's chosen type is its colour.
- Thran Portal's 1 life is an additional COST of its mana abilities (payable only at life >= 1, CR 119.4), not
  damage (the existing `tap_is_life_payment` path).
- Controller-aware: "Gates YOU control" (Gond), the Black Gate's life and Amulet's "you control" all read the
  entering land's controller.

### I3 cards (2026-11-17)

| Card | Tier | Params | Viewer bucket |
|---|---|---|---|
| Exploration | 3 | `static_extra_land_drops 1` | none of its own (`main_phase` land drop while a drop remains) |
| Burgeoning | 3 | `opponent_land_play_put_land_from_hand` | A: `dig` (which land / none) on the opponent's land play |
| Manabond | 3 | `endstep_may_dump_hand_lands_discard_hand` | A: `dig` (yes/no) at our end step |

Oracle text and mana cost pasted verbatim from `build/scratch/maze/oracle.jsonl`; modelling commentary in the bracket note.

**Exploration -- the multi-drop route (the search gap).** A Plan carries ONE `land_to_play`, so a second / third land in
one main phase was unfindable at any budget. Static drops: `StaticExtraLandDrops` (core/SpellEffects.h) seeds
`Player::bonus_land_drops_this_turn` at both untap resets (GameEngine::UntapStep and the rollout twin); a copy entering
mid-turn adds its drop in `FireEtbWatchers`. The search route is **site 10's extra-drop clause**, the
`TopLandNewlyPlayable` twin: `TurnSolver::LandDropSnapshot` is taken with the other section-window halves (both worlds:
ApplyPlanDirect's `drops_at_section`, AIEngine's `exec_drops_at_section`, each reset at every breakpoint decision point),
and `TurnSolver::LandDropNewlyOpen` arms site 10 at the section window when the section PLAYED a land (or a cast opened
a drop) and a drop is still open with a land in hand. `PlanOpensBreakpoint` marks such a plan (land played or an
extra-drop source cast, a drop left, a land left) so its continuation is fanned (searched), never BpUnbranchedCanon's
front. The continuation re-enumerates the land drop (`EnumeratePlansWithLand` offers it while `lands_played <
LandDropsAvailable`), each land with its own fetch / as-enters choices recorded on its OWN continuation Action
(`land_rad_mode`), never the outer plan's `rad_mode`.
*Completeness argument:* any legal sequence `c0 L1 c1 L2 c2 ... Lk ck` (c = cast sets in plan order, L = drops) is a
chain of plans `{c0, L1 at its slot, c1}` -> breakpoint -> `{L2, c2}` -> breakpoint -> ...: each link is an ordinary plan
(land slot + casts) of the state the previous link leaves, every link's land option set is the full enumeration, and
each breakpoint is a searched site-10 node (root turn) / fanned variant -- so at an unlimited budget every sequence of
drops is reachable; a budget re-prices nodes and never removes a link. Gated on `deck_has_static_land_drops` (stamped
from the param; Garth holds it open), so no other deck moves -- including Scale the Heights decks (see the open item).
Beyond the horizon the greedy drop takes EVERY open drop (`SimulateLandPlay` / `AIEngine::TryPlayLand` loop, the
permitted playout policy, deck-gated).

**Burgeoning and Manabond -- the crossing-land pin.** Every decision either card makes between the end of our turn and
our next untap is one ORDINAL of `GameState::scripted_crossing_lands` (int64, 12 x 5 bits), in event order: each
Manabond at our end step (yes/no; rank 0 = yes with a land in hand), then -- on a yes -- each dumped land's as-enters
value (only when it has more than one admitted value), then each Burgeoning trigger on the opponent's land play (one
rank per distinct (land name in hand, admitted as-enters value), then none LAST). The pin is consumed as read (shifted),
-1 = unpinned = every ordinal at rank 0 (the playout default beyond the horizon). `BoundaryFan` gains a crossing-land
digit (an inner odometer, the Defense-of-the-Heart shape: each ordinal's width learned per crossing through
`t_crossing_land_probe`), recorded on `Plan::crossing_land_choice` (written by `WritePostApplyStatePins`), threaded
through both one-rollout hosts (FS tail, BoundaryChoice) and the executor's crossing node
(`TurnSolver::ChooseRealCrossingLands`, called from `AIEngine::DecideCrossingFetch`), folded into every state key and the
plan signatures, expired at the turn boundary in both worlds. Scope = the morph face-up axis's (not human play, not a
narrowed playout / d0 runner / breakpoint enumeration, not a constant-leaf edge).
* Burgeoning: `ResolveBurgeoningTriggers`, from `ResolvePassiveOpponentLandTurn` right after the opponent's land PLAY.
  The land enters through `EnterLand(..., controller = us)` under `LandEntryNoFloatScope` and its own `LandEntryPinScope`
  (decision 15's rule): enters-tapped, Gond Gate, Amulet (no float), Gateway Plaza, Guild Summit's draw all for us.
  `DeckNeedsOpponentLandPlay` turns on for the param (decision 6).
* Manabond: `PerformEndStepManabond`, both worlds' end step right before Abiding Grace. A SIMULTANEOUS put: every land's
  tapped-ness is pre-decided against the pre-event board (CR 614.12: a Gond Gate in the batch untaps none of them), the
  lands enter with `fire_landfall = false` and their own pins, the rest of the hand is discarded (part of the
  resolution), then the batch's enter triggers resolve in put order (land extras, Gateway Plaza with its pin, landfall /
  Guild Summit -- those draws are kept).

**I3b -- the in-window audit of the crossing choices (2026-11-17).** The coordinator required that no Burgeoning
which-land / none, Manabond yes/no, or dumped-land entry choice is resolved by its unpinned front while search depth
remains. Instrument: `MTG_CROSSING_LAND_STATS=1` (TurnSolver.cpp `crossinglandcensus`, hooked in
`TakeCrossingLandRank`; `=2` prints each searched-frame / overflow hit with seed and turn), the face-up census's frame
classification plus an `overflow` count for a fanned crossing's 13th+ ordinal.
* **First census (I3 code, 60 d3 b10 + 30 d5 b10 games, seeds 91001 / 92001): 214 searched-frame hits** (seeds 91009,
  91042, 91051, 91058, 92015 ...; turns 7-8; widths 2-4). Root cause, traced in gdb on seed 91051 gi50 d3 b10: NOT the
  crossing axis. `MazesEndProvider::ProjectsAlternateWin` is a BOARD-level projection ("an untapped Maze's End plus {3}
  and ten Gate names"), and the plan enumeration's subset loop (EnumeratePlansWithLand, `ProjectsAlternateWin(state,
  {})`) labelled EVERY plan of such a turn `wins_this_turn`. SolveWithLookahead returns the first labelled candidate
  unexecuted at an interior node ("Candidates are sorted highest-value first..."), so a rollout turn adopted a plan
  that does NOT activate Maze's End (there: Cliffgate + a cast), without searching the turn, and the line's later
  crossing fell to its fronts inside the window. That is an in-window greedy pick of the whole turn, not only of the
  crossing. **Fix:** `DecisionProvider::AlternateWinNeedsAction()` (new virtual at the END; default false, so every
  other deck is byte-identical), true for MazesEndProvider, keeps the board-level projection off the per-plan label.
  The activation plan's win is found by execution. On the traced game the deck now wins T7 instead of T8.
* **Second fix, a host with no fan:** SolveWithLookahead's group-wave tranche scorer (`score_gw`) crossed with a bare
  `SimulateEndAndStartNextTurn` and adopted `pass_best = v` with no boundary pin. It now fans the crossing-land digit
  (`BoundaryChooseCrossingLandsByRollout`, the cl_only BoundaryFan, deck-gated) and records it. The OTHER boundary
  axes (echo, Defense of the Heart, face-up, upkeep tutor, ...) are still unfanned in that host: a pre-existing hole,
  OWED; fixing it moves those decks' play and needs a held-out A/B (USER).
* **Census after both fixes (same 90 games): searched-frame = 0, fanned overflow = 0** (playout 23.1M, outside-window
  59.5K, edge 0, real-crossing 0). d3 b10 avg 7.0667 over 60, d5 b10 avg 6.9667 over 30 (no profile, I4 cards still
  unimplemented). Cost note for the suite gate: ~43 s/game at d3 b10 and ~41 s/game at d5 b10 wall on 32 busy
  workers -- check `suite_gate.py --cost` early. The unbudgeted d3 rows ran past 0.3 h per game (probe stopped).
* **Greedy multi-land wrappers (Exploration).** `SimulateLandPlay` takes every open drop only where the single greedy
  drop already ran: an undecided plan (`land_decided == false`, i.e. a depth-0 static Solve: HorizonLeaf / D0Runner)
  and the extra-turn chain trial (depth 0). `AIEngine::TryPlayLand`'s callers are `m_lookahead_depth == 0` paths, plus
  one pre-existing executor fallback (AIEngine.cpp, the flood-keep drop after an UNSEARCHED draw continuation,
  `!bp_searched_here && hand > 7`), which now takes every open drop in this deck. Not a new site; recorded as owed
  with the fallback itself.

**I3b sound folds.** Added: decision 28 (Manabond "yes" with no land in hand, deck-scoped provider dominance). OWED,
not built (collapse-only, no proof written yet): an order fold for Exploration drops whose entries commute
(`LandEntriesCommute`; different-named Gates never fold, since the name set is the win condition); a multiset fold
for several Burgeoning triggers whose puts commute; a widening of the crossing pin past 12 ordinals if `overflow`
ever reads non-zero.

**I3b rules review** (`.claude/skills/mtg-rules.md`). Exploration: CR 305.2 additional land plays; the skill models
static grants as evaluated continuously, the engine folds them into the per-turn bonus at untap and on entry
(decision 21: a copy leaving mid-turn keeps its drop -- disclosed, reachable only through our own human-only
Charm mode 3). Burgeoning: triggers on the opponent PLAYING a land (the passive opponent's `play.played`), never on
a fetch's search or a put; one trigger per copy, each resolving against the current hand; the put is ours and is
not a land play (no `lands_played_this_turn`). Manabond: "If you do" -- revealing with no land still discards the
hand (Gatherer ruling), which is why decision 28 is a dominance, not an identity; the batch enters simultaneously
(CR 614.12: each enters-tapped-unless test and Gond Gate's replacement judged against the pre-event board, the
Genesis Wave precedent); enter TRIGGERS (Gateway Plaza, Guild Summit's draw, landfall) go on the stack after the
resolution, so the draws follow the discard -- correct. Controller-aware entry for the opponent-turn put (I2).
Ordering note: Manabond is fixed before Abiding Grace at the end step; a real controller orders its own
simultaneous end-step triggers (CR 603.3b), but no other end-step trigger exists in this list. No defect found.

### I4 cards (2026-11-17)

| Card | Tier | Params | Viewer bucket |
|---|---|---|---|
| Heap Gate | 3 | `produces WUBRG`, `any_color_filter`, `tap_treasure_cost {1}`, `tap_treasure_tap_other_subtype Gate` | A: `main_phase` activation (new `PermAbilityMode::TapTreasure`), then `target` (loyalty chooser) for WHICH Gate pays the tap cost |
| Baldur's Gate | 2 | `produces [C]`, `mana_per_creature_feeder_generic 2`, `mana_per_other_controlled_subtype Gate` (Legendary) | none (mana: the payment exemption) |
| Basilisk Gate | 2 | `produces [C]`, `tap_target_pump_cost {2}`, `tap_target_pump_power 1`, `tap_target_pump_tough 1`, `tap_target_pump_per_subtype_both Gate` | A: `target` on the live board (TapTargetPump); never offered autonomously here (no creature) |
| Talon Gates of Madara | 3 | `produces WUBRG`, `any_color_filter`, `hand_put_self_cost {4}` | A: `main_phase` from-hand action on the Channel kind (`channel=` verb, badged "put") |

Oracle text and mana cost pasted verbatim from `build/scratch/maze/oracle.jsonl` (`logs/maze/i4_add_cards.py`); modelling
commentary in each bracket note.

* **Heap Gate.** Mana is the Capital City shape. The Treasure ability is NOT a mana ability (CR 605.1a: it makes a
  token), so it is a searched `ActivatePermAbility` (`TapTreasure`, appended LAST to the enum and to all four ModeSpec
  tables, the plan-dump names). Costs in order, in BOTH apply worlds: the source's `{T}` (`SetPermTapped`), then the
  OTHER Gate's tap (`PayActivationOtherTap`, core/SpellEffects.h; `UndoActivationOtherTap` if the mana half then
  fails), then the `{1}`. Liveness (`PermAbilitySourceLive`) needs another untapped Gate we control; the enumeration
  also debits the source and the Gate the apply will tap before pricing the `{1}` (a Heap + one Gate board is never
  offered an unpayable activation). Which Gate: decision 30.
* **Baldur's Gate.** `IsScaledManaLand` accepts `mana_per_other_controlled_subtype`; `ScaledManaCreatureCount` counts
  the OTHER permanents the counting source's controller has with that subtype (tapped or not, `LandHasSubtypeNow` --
  Planar Nexus counts, an Islandized Gate does not), resolving a null source to the first scaled land as before.
  Every Three Tree City consumer (payer fed face, colour reachability, bounds, human fed face) reads it unchanged.
  Gond Gate: `ControlledSubtypeColors` adds WUBRG for a scaled-mana Gate (CR 106.7: it COULD produce any colour).
  Inherited approximation (Q4): the payer takes the fed mode only at net >= 1 over the free `{C}`.
* **Basilisk Gate.** The TapTargetPump resolver multiplies BOTH halves by `TapTargetPumpSubtypeBothMultiplier` (our
  Gates at resolution, the source included); the executor's log label too. Sorcery speed holds by construction
  (TapTargetPump is enumerated only in our main phases). Inert in this list (decision 3).
* **Talon Gates of Madara.** The `{4}` put rides `Action::Kind::Channel` (the Transmute precedent): one action per
  distinct hand name; both worlds check the card is still in hand BEFORE paying, then `ApplyHandPutSelf` (EnterLand
  under the controller, its own pin scope EMPTY so an outer pin cannot leak onto it; not a land play). Main-phase
  timing: decision 32. Trailing order: decision 31. ETB phase-out: not modelled (decision 33).
* **Guild Summit order (decision 10, now built):** `MazesEndProvider::EtbTapSubtypeOrder` -- decision 30.

**I4b verification (2026-11-17).** The I4 test file compiled as written; one assertion was wrong (the Talon control
game holds one Gate in its library, so it can never reach ten names and `PlayOutFrom` returns -1, not a win after turn
5) and now asserts no win on turn 5. I4 tests 7/7; unit suite 3425/3425. `provider_audit.py --check` exit 0 (Maze's End
still "NOT AUDITED": no profile). `audit_viewer_decisions.py --no-sweep` exit 0 (advisory oracle notes only, every one
with a note; Talon's phase-out `target` is decision 33).

* **Crossing census found one more host hole (fixed, `61fd80c65`).** `MTG_CROSSING_LAND_STATS=2` over the I3b games
  (seeds 91001 d3 b10 x60, 92001 d5 b10 x30) read 15 searched-frame decisions, all on seed 91052 gi51 at T8
  (max_turns 8): Manabond's yes/no and two dumped lands' as-enters values. Root cause (gdb, RelWithDebInfo): the
  root pass's overrun ceiling (`m_used == m_overrun_limit`) tripped inside a rollout's LAST turn; that turn's nested
  solve bailed and returned its unsearched front plan (no boundary pin), and `SimulateToEndImpl` crossed anyway --
  its overrun poll sits at the START of a turn and no later turn exists. Fix: before that crossing, a rollout whose
  ceiling has tripped (`OverrunQuiet`) on its last turn (`turn_number >= max_turns`, no pending extra turn) returns
  no-win with an invalid leaf, as the turn-start poll does. Result-identical by construction: the bail already
  dirtied `g_fs_trunc_events` and the budget binds (no cache stores the result), the ceiling's owner drops the
  tripped pass, and the anytime rescue needs a STRICTLY earlier proven win, which a crossing out of the horizon
  cannot produce. Measured: the traced game's log is identical, both census jobs' `.wins` are identical before and
  after, and the smoke is byte-identical. Census after: **searched-frame 0, overflow 0** (playout 51.3M, outside-window
  54.8K, edge 0, real-crossing 0). Not covered: the same post-trip crossing on a NON-final turn (the next turn-start
  poll discards it, so it is wasted work, not a play decision; the census saw none). Stopping there too is not
  result-identical for a deck that can deck the opponent during the crossing (the loop-top `opponent_decked` win
  check precedes the poll), so it was left alone.
* **Cost (90 census games, 32 workers, no profile).** d3 b10: **108.8 s/game** wall, 1.48M units/game (I3b: 43 s,
  0.67M; 2.2x the units), avg win turn 6.5667 (I3b 7.0667). d5 b10: **54.1 s/game**, 0.71M units/game (I3b: 41 s,
  0.66M; 1.08x), avg 6.6667 (I3b 6.9667). Tail: seed 91021 gi20 (d3) 1,253 s / 26.1M units; seed 91054 gi53 (d3)
  1,192 s on only 2.9M units (wall per unit ~40x the median: units under-price that game's work). The I4 cards are
  not attributed separately (no A/B; the coordinator's performance phase). `suite_gate.py --cost` has NOT been run;
  at this rate the 3x-Hinata gate is at risk.
* **Branching check (step 4).** Executed play over the 90 games: Heap Gate's Treasure made 63 times; Talon Gates put
  from hand 0 times (played as the land drop 14 times). **No fold added** -- none could be proven sound:
  - *Heap's Treasure.* It turns `{1}` + Heap's `{T}` + another Gate's tap into one mana on a later turn (mana banking:
    a real line, and a Treasure never spent before the game ends is still not a reason to prune). Two candidate folds,
    neither built: (a) a Treasure made and sacrificed in the SAME turn is dominated by Heap's own `{1},{T}` filter,
    which leaves the cost Gate untapped -- but whether the payer sacrifices it is the payment exemption's choice, not
    a plan branch, so there is nothing at enumeration to cut; (b) making it in a plan's BASE segment when the plan has
    a breakpoint continuation (Guild Summit / Gateway Plaza draws, Sylvan Scrying) looks dominated by making it in the
    continuation (same resources, more information; the cost Gate could instead feed Summit's tap-draw) -- but that
    needs an enumeration-time test for "this plan will reach a continuation" and a written commute proof against every
    continuation spend. OWED as an investigation, recorded as a **cost driver**: one more searched activation on most
    mid-game turns (Heap untapped + another untapped Gate + `{1}` spare), roughly doubling those turns' activation
    subsets. The cost Gate itself is not branched (decision 30), and with one Heap in the list it is one activation
    per turn at most.
  - *Talon's put.* Candidate dominance: a put in a plan whose land drop ends unused, where Talon could have been that
    drop (identical board, `{4}` richer). Not provable at enumeration: the drop can be filled later in the same turn
    by a land that reaches hand mid-turn (Maze's End's own return, Summit / Plaza draws, Sylvan Scrying). Left
    searched, **cost driver** (one Channel option while Talon is in hand and `{4}` is open; never adopted in 90
    games). Identity already holds: a Talon staged as the land drop is skipped by the put emission (`m_is_staged`),
    so "play it AND put it" is never enumerated.

**I4 rules review** (`.claude/skills/mtg-rules.md`). *Heap Gate:* the Treasure ability is not a mana ability (it
makes a token: CR 605.1a), so it is activated and resolves like any ability; costs `{1}`, `{T}`, tap another untapped
Gate may be paid in any order (CR 601.2h via 602.2b) -- the engine taps the source, then the other Gate, then pays the
`{1}` from what is left (`AvailableManaPool` is read AFTER the cost Gate is tapped, so the `{1}` can never be paid by
that Gate), and undoes both taps when the `{1}` fails; tapping an untapped Gate as a cost is not a `{T}` cost, so
summoning sickness is irrelevant (CR 302.6); the source and tapped Gates are excluded. *Baldur's Gate:* X = OTHER
Gates we control at resolution, tapped or not (mana abilities resolve immediately); Planar Nexus counts (every
nonbasic land type), an Islandized Gate does not. Gond Gate under CR 106.7 reads what a Gate COULD produce ignoring
costs and amounts, so Baldur's (any one colour), Heap and Talon (`{1},{T}`: any colour) make Gond rainbow; Gond never
reads another Gond (the loop rule). Baldur's X is never 0 while Gond is out (Gond is another Gate). *Basilisk Gate:*
X counts every Gate we control at RESOLUTION, the source included; sorcery speed by construction. *Talon Gates:* the
`{4}` put is an activated ability from hand that uses the stack and is instant speed (decision 32 folds it to our main
phases); putting a land onto the battlefield is not playing it (CR 305.4: no land drop spent, not counted by
`lands_played_this_turn`, no Burgeoning trigger for us); it is a Gate entering, so Guild Summit draws and landfall
fire; enters untapped (no tapped clause; Amulet inert). Its ETB "up to one target creature phases out" may choose
zero targets -- with no creature on either side in this goldfish every choice is the same game (decision 33,
PROVISIONAL). No defect found.

## Decisions and provisional deferrals (USER sign-off pending)

**USER rulings on decisions 1-33 and the viewer (2026-11-17, relayed by the coordinator, VERBATIM):**
"D1. That is fine. These are the kind of things we could have as an option, but there is not much point in having
it on. B1. I would probably heuristically prune this. A common answer from play is tap none, but tap all unused is
also reasonable. Possibly the best is to just use whatever mana we don't spend in the line? B2. Apparently it
depends on whether it is the gate being put into play tapped. If it is, it enters tapped. Otherwise untapped. B3. I
guess it can be deferred. The only mode we care about heuristically is the first. B4. Okay, I guess we can wait on
that. B5. Yes, we should use the simulated-opponent model which will enable burgeoning."
Mapping (coordinator): D1 = `draw_on_other_subtype_enter` INERT in the viewer -- RULED, confirmed, no change.
B1 = decisions 2/8/10 (Guild Summit's count) -- RULED: a provider prune to at most {0, all untapped, all untapped
minus what the rest of the plan still needs} (work item B1, A/B on held-out seeds). B2 = decision 5 (Gond Gate) --
see the CORRECTED B2 ruling below (the first reading, "an effect's tapped put wins", was a misreading and was reverted
unbuilt). B3 = decision 9 -- RULED, APPROVED deferral (moved to
"Approved deferrals"). B4 = decision 33 -- RULED, APPROVED deferral (moved). B5 = decision 6 (Burgeoning on the
passive-opponent land model) -- RULED, confirmed, no change.

**USER pruning directives + the corrected B2 (2026-11-17, relayed by the coordinator, VERBATIM):**
"We should prune a bunch of things including land search targets, colours for lands that get to choose. Typically
those choosing lands only should target Green and Blue. Once we have one green we can get blue and once we have one
blue the rest can go to green. Gates ablaze can get heurstically skipped, so we don't need Red. B2. No it should
enter untapped if Gond Gate is already active. I'm saying that Gond Gate's effect does not kick in until it enters
which means Archdruid's Charm for Gond Gate will enter tapped unless there is another Gond Gate, but with a Gond Gate
out it would enter untapped."
Decision 5 is therefore RULED AS BUILT: a Gond Gate already on the battlefield untaps every Gate we put, including a
forced-tapped put (Archdruid's Charm mode 1); Gond Gate does not untap itself (no other Gond -> tapped; Amulet still
untaps it). Verified by unit "Maze's End P1c (USER B2)". Work item P (coordinator's mapping): P1 chosen colours
collapse to ONE (no green source -> G; else no blue source -> U; else G; neither offered -> first offered); P2 Gates
Ablaze never autonomous, spendable premise G/U only; P3 land-search target classes (proposal, measured); P4 list
further wide axes for a USER ruling (not built). All provider-only, flag default ON, one pooled held-out A/B with B1.

**USER rule for P3, land search targets (2026-11-17/18, relayed by the coordinator, VERBATIM; REPLACES the class
proposal):** "Maze's end should remain a candidate if you don't have 2 or 1 and Amulet of Vigor. Sometimes you want
multiples to keep things running. Baldur's gate is a good call as it generates a lot of mana and so is Gond Gate if
you are missing it. Otherwise we should pick one singleton we don't have yet to increase variety. We should definitely
prune this decision down to 1-4, but preferably closer to 1-2 which should be possible. Maze's End when we have none
is usually the right call so we might be able to prune that case."
Coordinator's reading (Sylvan Scrying, Archdruid's Charm mode 1, any other land tutor in MazesEndProvider): no Maze's
End -> {Maze's End}; otherwise, in order: Maze's End unless we have 2+ (or 1 + an Amulet of Vigor), Baldur's Gate if we
control none, Gond Gate if we control none, ONE new-name Gate (best by enters-untapped given Amulet/Gond, then
ManaSourceRank; the P1 colour rule may pick it). Maze's End's own fetch: {Baldur's if missing, Gond if missing, one
new-name Gate}. 1-4 options, usually 1-2; fail-to-find stays where it is offered. Burgeoning / Manabond only if
clearly the same decision, else P4. "Have" = battlefield + hand (to be stated in the report). Measured inside the B1 +
P1-P3 pooled held-out A/B; if "Maze's End when we have none" loses vs a second option, report those games.
USER amendment to P3 case 2 and the Maze's End fetch (VERBATIM): "For 2 there I would probably skip the "one gate
name" option until Baldur's gate is gone and we have Gond Gate. That would mean we can keep it at 1 or 2 choices
almost the whole time." Rule: the one-new-name option only when Baldur's Gate is no longer available (controlled, or
not in the library) AND we have Gond Gate (controlled, or none left in the library -- so the menu never empties).
Before that: Maze's End (when its condition holds) + Baldur's if missing + Gond if missing. Same for Maze's End's own
fetch. Fail-to-find unchanged.
USER clarification (VERBATIM): "Have for Gond Gate here includes in hand. We shouldn't search it if we have it
anywhere." So Gond Gate is offered only with none on the battlefield AND none in hand, and "we have Gond Gate" in the
new-name gate means battlefield or hand. Baldur's Gate: the same "anywhere" reading was first the COORDINATOR's
default; now CONFIRMED by the USER (2026-11-18, relayed by the coordinator, VERBATIM): "Baldur's gate would also be
fine that way, but it doesn't matter because there is one copy. If it is in hand you cannot search for it."
USER ruling on Maze's End's count (VERBATIM): "Same with Maze's end, actually. Having 2 in hand means we don't need
anymore." So "have" for Maze's End = battlefield + hand (USER): 2+ anywhere -> not offered; 1 anywhere + an Amulet of
Vigor -> not offered.


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
9. **Archdruid's Charm mode 2 is NOT modelled** (RULED B3 2026-11-17: APPROVED deferral, see Approved deferrals): it needs a
   target creature we control, the list has none and makes none, so it can never be chosen (CR 601.2c).
   The card's bracket note says so.
10. **Guild Summit's which-Gates order is ManaSourceRank ascending** (decision 2). BUILT in I4 (decision 30). Protecting Gates with
   non-mana {T} abilities is NOT yet implemented: I4 must add it in `MazesEndProvider::EtbTapSubtypeOrder`.
11. **A land that has already left before `OnLandEntered` runs** (a self-bouncing Karoo) draws nothing off
   Guild Summit. No Gate does that.
12. **`TutorSearchWidth` 64** for the whole deck (coverage, not a heuristic).
13. **Archdruid's Charm mode 3 is offered only in human play** (and `DecisionUnpruned`); autonomously the
   provider withholds it (every legal target is our own permanent). Executor and rollout share one
   resolver.

14. **The land-entry pin rides `Plan::rad_mode` / `LandPlayOptions::rad_mode`** (I2) rather than a new plan
   field: its meaning is the land's (a colour; Plaza pay/sacrifice; Mariposa's rad mode) and no land carries two
   such choices, so every land route that already threads the rad pin (deferred land, Karoo, breakpoint
   continuation, executor) carries it unchanged. An implementation choice, recorded for review.
   **Checked (I2b): no plan can need two values in the field.** `Plan::rad_mode` / `LandPlayOptions::rad_mode` is
   read only by the plan's ONE land drop (`PlayLandCore`, LandPlay.cpp: Mariposa's rad at the tapped-ness step, the
   colour lock, the Plaza payment); no cast, activation or effect put reads it. Mariposa's post-pass gates on
   `etb_optional_tapped_rad` and the pin fan on `LandHasEntryPinChoice`, and no land has both. Effect puts carry
   their pin on their own channel (Maze's End's fetch: `Action::chosen_x`; Archdruid's Charm: `Plan::tutor_choice`,
   decision 15), so a plan that drops a CLB Gate AND Charms another carries two independent pins. The one future
   collision is a SECOND land drop in one plan (Exploration, I3): each extra drop must carry its own pin (the
   per-action `Action::land_rad_mode` already exists for a deferred drop), never the plan field.
15. **Archdruid's Charm's put is SEARCHED per (land, as-enters choice)** (I2b, on the coordinator's ruling; was
   PROVISIONAL "the provider picks", an unsearched pick inside the search window). The resolution tutor axis
   (`Plan::tutor_choice`) now fans, for a `tutor_land_to_battlefield` card whose candidate land has a land-entry
   choice (a CLB Gate / Thran Portal colour, Gateway Plaza's pay-or-sacrifice), one variant per (candidate index,
   admitted pin), the front candidate included (the base plan keeps the provider's default, so one base/variant
   pair is a duplicate -- not a loss). The pin rides the same int: `tutor_choice = index + 4096 * (pin + 1)`
   (`EncodeTutorEntryPin`, core/SpellEffects.h), so every site that carries, hashes, dedups, captures or restores
   the tutor pin carries the entry pin with no edit; the indexing consumers decode (`TutorChoiceIndex`) and
   `PerformLandTutorToBattlefield` installs the pin (`LandEntryPinScope`) for its put only. `LandEntryPinCandidates`
   RESTRICTS (decision 1/20) and orders; it picks nothing. Byte-identical for every other deck (no other land has
   a choice: checked over cards.json). Limits, inherited from the tutor axis itself and disclosed: the axis varies
   ONE tutor per plan (the first in action order), so a plan that casts Sylvan Scrying before the Charm leaves the
   Charm's find AND its pin at the provider's front; the name-bound axis (`MTG_TUTOR_AXIS_RESOLVE=0`, non-default)
   is not extended; beyond the horizon / d0 runner the default applies (permitted greedy).
   **Audit of the deck's other effect-put routes:** Maze's End's fetch -- searched (`chosen_x` = pin + 1, I2);
   Guild Summit's count -- beyond-horizon only (decision 8); Burgeoning and Manabond (I3) and Talon Gates /
   any I4 put -- not yet implemented: each MUST carry its own pin (`LandEntryPinScope` per land) or reopen this gap.
   No other route in the current tree puts a land with a choice.
16. **Gateway Plaza's pay-or-sacrifice uses the `dig` chooser (Mihail's "pay {1}" shape), not `echo`**
   (supersedes decision 4's wording): the echo chooser is AIEngine's upkeep hook and cannot be reached from the
   shared land-entry code; the dig shape is the existing core-level pay-or-not precedent. Asked only when the
   {1} is affordable. USER sign-off pending.
17. **Gateway Plaza's trigger resolves before Guild Summit's draw / landfall** (the land's own triggers before
   the watchers). A real controller may order Summit's draw first and see the card before paying; the searched
   pin is fixed by the plan before either, so only a human loses that peek. Disclosed.
18. **Multi-Amulet float is automatic** (weakly dominant: no mana burn, the land ends untapped either way); its
   colour follows the shared tap-first commit (the line's owed colour, else `FloatSourceTapCommitted`: the
   mana-payment exemption). No float for a land whose tap has a side effect (Thran Portal's 1 life -- a small
   loss under 2+ Amulets, disclosed), a feed-only converter (Planar Nexus, Heap Gate: they never enter tapped
   anyway), a nonland, or an entry outside its controller's main phase (`LandEntryNoFloatScope`, which I3's
   opponent-turn / end-step puts must set).
19. **The Black Gate under an Amulet of Vigor declines the 3 life** (`MazesEndProvider::LandEntersUntapped`: it
   enters tapped and is untapped -- the same land, life kept; a sound dominance). Under a Gond Gate it enters
   untapped and nothing is paid (the predicate's first branch).
20. **Spendable-colour restriction premise** (decision 1's prune): the colours are read from the MANA COSTS of the
   nonland cards in the controller's zones (G/U/R for this list); activated-ability pips are ignored because the
   only one in the list (The Black Gate's {1}{B}) needs a creature target and the list has none. Re-check if the
   list changes (the restriction is recomputed at runtime, the premise is not).
21. **Exploration's extra drops are folded into `bonus_land_drops_this_turn`** (seeded at each untap from the copies
   in play, +1 when a copy enters). A copy LEAVING mid-turn does not take its drop away; only our own Archdruid's
   Charm mode 3 (human-only, decision 3) could exile one. Disclosed in the bracket note.
22. **The multi-drop route is gated on `deck_has_static_land_drops`.** Scale the Heights (`grants_extra_land_drop`)
   decks keep the old hole: a second drop in one main phase is unfindable for them. OPEN QUESTION (USER): extending
   the route moves their play and needs a held-out A/B.
23. **Burgeoning's menu** is the distinct land names in hand order, each crossed with its admitted as-enters values,
   then "none" LAST. Rank 0 (the first land) is the beyond-horizon playout default.
24. **Manabond's rank 0** is yes when the hand holds a land, else no (the playout default). See 28 for the no-land
   restriction.
25. **Manabond and Burgeoning share ONE crossing pin** (an odometer over event-ordered ordinals, 12 max) rather than
   separate plan axes. A 13th+ real decision in one crossing takes its front unsearched; the census (29) counts it
   as `overflow` (0 seen).
26. **Human play** asks which land / yes-no on the `dig` chooser, and the land's colour or Plaza prompt follows at the
   entry. Autonomous play fans (land x admitted entry pin) in one rank (decision 15's rule).
27. **Manabond's dumped lands resolve their enter triggers in put (hand) order, after the discard.** A real
   controller may order its simultaneous triggers; the only order-sensitive pair in the list is Gateway Plaza's
   pay before / after Guild Summit's draw, the same disclosure as decision 17.
28. **Manabond "yes" with NO land in hand is pruned** (I3b; `DecisionProvider::CrossingDumpWithoutLandDominated`,
   true only in `MazesEndProvider`). "Yes" there is NOT the same as "no" -- it still reveals and discards the hand --
   so this is a deck-scoped DOMINANCE restriction, not an engine identity fold: nothing in this list reads the
   graveyard, rewards a discard or an empty / smaller hand, and the cleanup discard is ours to choose, so a superset
   hand is never worse. The crossing-pending predicate mirrors it. Human play still asks. PREMISE: re-check if the
   list gains a madness / threshold / delirium / hellbent / hand-size card.
29. **The in-window audit for the crossing choices** (I3b, coordinator requirement): `MTG_CROSSING_LAND_STATS=1`
   (TurnSolver.cpp `crossinglandcensus`, the face-up census's shape) classifies every UNPINNED crossing decision with
   a real choice by frame, plus fanned-crossing overflow past the 12 ordinals. Result in "I3 cards" below.

30. **The Gate TAP ORDER** (I4; Guild Summit's which-Gates, decision 2/10, and Heap Gate's cost Gate) is
   `MazesEndProvider::EtbTapSubtypeOrder`: tier 0 pure-mana Gates, tier 1 Baldur's Gate (a scaled mana land: one tap
   can be worth several mana), tier 2 a Gate with a LIVE non-mana `{T}` ability (Heap Gate's Treasure; a targeted
   pump -- Basilisk Gate, The Black Gate -- only while we control a creature, so in this list they read as the plain
   `{C}` / `{B}` sources they are); ManaSourceRank inside each tier. An ORDER, never a restriction; the Summit count
   and whether Heap activates stay searched. Heap Gate's cost Gate rests on the same mana-payment-exemption reading as
   decision 2 (the alternative is a searched pin per Gate class). Maze's End itself is not a Gate, so it is never
   eligible. USER sign-off pending.
31. **Talon Gates' put runs FIRST in its plan's trailing pass** (`MazesEndProvider::ActivationOrderRankForAction`,
   rank -1; every other action keeps rank 0, so their order is unchanged). Sound: the put only adds resources (an
   untapped Gate -- a new name for Maze's End's win check, an "other Gate" for Heap's cost -- and its own `{T}` mana)
   and its `{4}` comes from the same pool in any position. Without it the pass runs in emission order and Maze's End's
   activation (the win check) preceded the put, so "put Talon Gates, then win" was unfindable in one plan.
32. **Talon Gates' put is offered in OUR main phases only.** The real ability is instant-speed. Sound fold: in a
   goldfish the opponent's turn offers no mana or information a main-phase put lacks -- an end-step put with the same
   untapped mana leaves the same board at our next untap, a Burgeoning put of another land commutes with it, and a
   main-phase put sees Guild Summit's draw earlier. So no opponent-turn put route (and no no-float scope) exists.
33. **Talon Gates' ETB "up to one target creature phases out" is NOT modelled** (RULED B4 2026-11-17: APPROVED deferral) (inert collapse, disclosed in the
   bracket note; the engine has no phasing). Every choice reaches the same game in a goldfish: the list has no creature
   of ours and makes none, and the passive opponent's creatures never attack or block. Not offered as a viewer choice.
   PROVISIONAL (USER sign-off pending) -- re-check if the list gains a creature.

## Performance (P1 / P1b, 2026-11-17)

**Yardstick:** Hinata 5,170 ms/game; the suite-admission limit is 3x = **15,510 ms/game** at the deck's most
expensive searched shape (USER 3x rule). Census set: d3 b10 60 games (seed 91001) + d5 b10 30 games (seed 92001).

### Attribution (P1)
* ~80% of a tail game's wall is the GREEDY subset walk (`SolveUncachedBody`: the horizon playout and the d0 runner),
  which the budget did not charge, so units and wall diverged (50-430 us/unit). Inside it, per-subset recomputation of
  board-only predicates (`ProjectsAlternateWin` + its Gate-name scans, `FreshMintSpendableNow`) was ~30-35% of wall,
  and Maze's End's per-Gate-name TapTutorPut family (~20-25 options per copy) multiplied every subset.
* Quality side: the no-win leaf grade was opponent life, which never moves for this deck (18.6M of 33.9M published
  leaves life-equal ties), and the d0 greedy left Maze's End's activation to the tie-break (eval 0): it won 20/60 by
  T8 (avg 8.25). d3 leaned on blind T8 rollouts, hence d3 (109 s/game) costing more than d5 (54 s/game).

### Fixes (all on `wip/maze-end`)
1. **Board-only predicate hoist** (P1, every deck): `FreshMintSpendableNow` and (provider opt-in
   `AlternateWinProjectionIgnoresCasting`) `ProjectsAlternateWin` asked once per walk. VERIFIED byte-identical:
   seed 91005 gi4 d3 b10 with the fold off reproduces the pre-P1 units (371,372) and game log (runId aside).
2. **Canonical greedy tie-break past 32 candidates + the tutor-target fold** (P1b). The greedy walk breaks an exact
   (wins, rank) tie by the smaller `int` subset mask, but `1 << j` wraps from j = 32, so subsets aliased and the pick
   among exact ties depended on VISIT ORDER. `MTG_GREEDY_FOLD_VERIFY` on gi4: 75,881 of 186,698 walks differed, every
   one past 31 candidates, 0 at or under -- the fold (`MTG_GREEDY_TUTOR_TARGET_TIE`) was an identity fold whose
   result moved only because the tie-break was not canonical (e.g. "Exploration + Heap Gate" vs the same plus an
   eval-0 Maze's End activation, both v=101). Fix: `CanonSubsetMaskLess` compares the un-aliased masks (exactly
   `mask < best_mask` inside 0..31, so a walk with no index >= 32 is unchanged); the mana-verdict cache, keyed on the
   same int mask, is armed only up to 32 candidates; the fold keeps the member the tie-break prefers. After it:
   verify reads **0 of 99,174** walks differing; gi4 fold on/off game logs identical; fold on 20.8 s vs off 33.6 s.
   The fold is KEPT (default ON) as a sound identity fold. Other decks: changes only walks with >= 33 candidates
   whose best ties exactly -- see the smoke result below.
3. **Alternate-win progress evaluation** (P1b; evaluation, not a prune -- every option stays enumerated):
   * `DecisionProvider::NoWinLeafProgress` (end of class, default 0): subtracted from the graded no-win leaf in its
     own units. `MazesEndProvider`: distinct Gate names x 2,000,000 (life-equivalent: ten names = twenty life), plus
     Maze's End held (battlefield or hand) 500,000, plus each new-name Gate in hand 100,000 (at most 4). Lever
     `MTG_MAZE_LEAF_PROGRESS` (default ON).
   * `DecisionProvider::TapTutorPutEval` (end of class, default 0): the plan-scoring eval of a TapTutorPut fetch.
     `MazesEndProvider`: 200 for a Gate name we do not control, 0 for a duplicate or the fail-to-find. This is what
     the greedy walk (beyond-horizon playout / d0 runner) ranks by, so the playout now activates Maze's End for
     progress instead of leaving `{3}` idle. Lever `MTG_MAZE_FETCH_EVAL` (default ON).
   * Measured (census set + 60 d0 games, 4 arms innermost, one pooled manifest run as 4 resumed sub-batches because
     of the RSS cap -- see Memory):

     | shape | arm | avg win turn | wins <= T8 | ms/game | units/game | vs off (better/worse) |
     |---|---|---|---|---|---|---|
     | d0 | off | 8.2500 | 20/60 | 6 | - | |
     | d0 | leaf | 8.2500 | 20/60 | 6 | - | 0/0 (inert at d0) |
     | d0 | eval | 7.1167 | 46/60 | 5 | - | 35/0 |
     | d0 | both | 7.1167 | 46/60 | 4 | - | 35/0 |
     | d3 b10 | off | 6.5333 | 56/60 | 46,380 | 1,532,303 | |
     | d3 b10 | leaf | 6.6000 | 56/60 | 44,758 | 1,568,822 | 6/10 |
     | d3 b10 | eval | 6.4833 | 57/60 | 28,782 | 1,247,026 | 11/9 |
     | d3 b10 | **both** | **6.4167** | **59/60** | **29,263** | 1,262,083 | 15/10 |
     | d5 b10 | off | 6.6667 | 26/30 | 22,498 | 712,449 | |
     | d5 b10 | leaf | 6.6000 | 28/30 | 18,972 | 642,154 | 2/2 |
     | d5 b10 | eval | 6.6333 | 28/30 | 14,822 | 585,511 | 3/4 |
     | d5 b10 | **both** | **6.6333** | **28/30** | **14,277** | 551,039 | 3/4 |

     ADOPTED (both ON): win turn better at every shape (d3 -0.117, d5 -0.033, d0 -1.133), ~1.6x cheaper. The leaf
     term alone is slightly worse at d3 (6.600) but helps on top of the fetch eval (6.417 vs 6.483). USER sign-off
     pending on the weights (2 life per Gate name; 200 per new-name fetch) -- an evaluation choice, surfaced.

4. **Greedy-walk charge weight (`SolveChargeWeightOptIn`) -- swept, NOT adopted.** Census set with fixes 1-3 on,
   5 arms innermost in ONE batch (10 workers, `same_seed_inflight` 1; build/scratch/maze/p1b/s3):

   | shape | W | avg win turn | wins <= T8 | ms/game | units/game | vs W=0 (better/worse) |
   |---|---|---|---|---|---|---|
   | d3 b10 | 0 (current) | **6.4167** | 59/60 | 30,470 | 1,262,083 | |
   | d3 b10 | 1 | 6.5500 | 59/60 | 21,973 | 1,490,767 | 6/13 |
   | d3 b10 | 4 | 6.4833 | 58/60 | 28,364 | 1,302,470 | 3/7 |
   | d3 b10 | 8 | 6.4833 | 57/60 | 27,238 | 1,239,042 | 2/6 |
   | d3 b10 | 16 | 6.4667 | 59/60 | 27,704 | 1,247,209 | 1/3 |
   | d5 b10 | 0 (current) | 6.6333 | 28/30 | 12,781 | 551,039 | |
   | d5 b10 | 1 | 6.6000 | 28/30 | 8,323 | 646,198 | 4/3 |
   | d5 b10 | 4 | 6.7000 | 28/30 | 12,207 | 550,547 | 0/2 |
   | d5 b10 | 8 | 6.6667 | 28/30 | 12,793 | 538,539 | 0/1 |
   | d5 b10 | 16 | 6.7000 | 28/30 | 13,479 | 548,600 | 1/3 |

   No W brings d3 b10 under 15,510 ms/game, and every W is worse on d3's win turn (W=1 by 0.133). DEFAULT: **no
   charge (W = 0)**, USER sign-off pending (precedent: Snow's W was the USER's). W=1 is cheaper AND not worse at d5
   (8.3 s, 6.600) -- worth surfacing if the suite ends up gating Maze's End at d5 only. d3's cost is concentrated:
   seed 91021 gi20 alone is 465 s (20.4M units, ~7.7 s of the 30.5 s/game average; ~23.1 s/game without it).
   **Status vs the 3x rule: d5 b10 12.8 s/game is under 15,510; d3 b10 30.5 s/game is ~5.9x Hinata -- over.** (ms
   here are under 10-worker contention, not the suite's pooled measurement; `suite_gate.py --cost` not yet run.)

### USER rulings on the P1b questions (2026-11-17, relayed by the coordinator, VERBATIM)
"A1: Okay I guess. We may want the tuning sweep, because I have no idea how to pick these numbers. A2. Okay, let's
see how it does. A3. Okay, I suppose 1 seems okay, but we can play with it more as needed. A4. The equivalent
guildgate thing might be good. The other change sounds dicey for the reason you mentioned. If we have no land drop,
though, (and hence Maze's end let's us use the drop it might make sense to use. A5, It's weird to me that d3 is so
expensive. We might want to look at that in isolation. I'm okay with skimping on d3, though, in favour of play
settings."
(A1 = progress-eval weights; A2 = canonical tie-break / tutor fold; A3 = playout charge weight; A4 = equivalent
Guildgates / "always activate Maze's End"; A5 = d3 b10 cost vs the 3x rule.) Coordinator's reading: adopt W = 1
(lever kept); study d3 cost in isolation (start with seed 91021 gi20), d3 quality loss is not a veto, d3 stays in
the rows / gate; BUILD the exact Guildgate-equivalence fold (sound, default ON, runtime premise check) and any
inexact equivalence default-OFF with an A/B; "always activate" rejected as a prune -- at most an ORDER (activation
first when no land is left to play) inside the window, playout policy beyond it, default-OFF lever, measured at d5
b10 and d3 b10 on held-out seeds; weight tuning sweep (heuristic-optimization skill) after those; the W=1 cost
measurement also carries mulligan-GENERATION shapes d1 b1 and d1 b3 (~30 games each).

### P1c (2026-11-17/18)
* **Unit regression found and fixed.** With P1b's fetch eval ON, unit "Maze's End I3: the search finds THREE land drops
  in one main phase" failed (turn 6 vs 5 at d3, budget 0; `MTG_MAZE_FETCH_EVAL=0` passed). Mechanism: the breakpoint
  continuation list is value-ranked and wave 0 reaches ranks 0..W-1 (W = 2); after Maze's End is played (two drops
  left, Rakdos + Selesnya in hand) the two "Gate + fetch now" plans (v = 200) took ranks 0-1 and the plain land play
  the win needs fell to rank 3. Fix `2e3fb25c3`: `MTG_MAZE_FETCH_DEFER` (default ON) -- the fetch eval is 0 while a
  land drop is open AND a new-name Gate is in hand, read on the state the activation is enumerated in (after the
  plan's own land play). Units 3425/3425 (3426 with the B2 test). A/B vs P1b (census set + 60 d0; one pooled batch, 8
  workers -- MEMORY exception -- `build/scratch/maze/p1c/ab1`, peak RSS 11.1 GB, 17.0 min):

  | shape | P1b | defer | defer vs P1b (better/worse) |
  |---|---|---|---|
  | d0 | 7.1167 / 46 wins | 7.1167 / 46 (60/60 digests identical) | 0/0 |
  | d3 b10 | 6.4167 / 59 / 27,967 ms / 1.26M units | 6.5167 / 58 / 33,492 ms / 1.41M units | 7/11 |
  | d5 b10 | 6.6333 / 28 / 11,700 ms / 551K units | **6.5667** / 28 / 14,411 ms / 591K units | 4/2 |

  KEPT ON (it restores a search-completeness unit; d5 = play settings is better, d3 worse -- USER A5 accepts d3
  skimping). USER-facing; surfaced.
* **Smoke (P1/P1b/P1c tree) vs smoke-i4: 212/219 byte-identical, 7 cases moved** (saved `build/scratch/maze/smoke-p1b/
  wins`; GT NOT accepted; 168 pass / 51 fail = the 44 stale + these 7). Bisected with temporary env gates in one
  pooled 7-case batch: reverting BOTH the canonical tie-break and the mana-cache `<= 32` gate reproduces all 7 i4 files
  byte-for-byte; reverting the tie-break alone reproduces 6 (alchemist d0 also needs the cache gate). So every move is
  the `1 << j` (j >= 32) aliasing fix: exact (wins, rank, value) ties in greedy walks with >= 33 candidates, plus, for
  alchemist d0, the mana-verdict cache returning an ALIASED subset's verdict (a stale-verdict bug). Direction (win
  turn, loss = 99): extraturns d0 33 better / 0 worse; jumbo d0 5/15 (sum +197); melira d0 2/4; alchemist d0,
  extraturns d3 (1 game), extraturns d5 (1 game), vampires d0 (1 game) digest-only, same win turns. Only d0 win
  turns move. USER-facing (UB fix; other decks are not byte-identical).
* **B2 verified as built** (USER's corrected ruling): unit "Maze's End P1c (USER B2)" -- Charm (ForcedTapped) ->
  Gond Gate with no other Gond: tapped; with an Amulet: untapped; with another Gond out: untapped; Charm -> Simic
  with a Gond out: untapped. No code change.

### P1d (2026-11-18): the USER-directed provider prunes (B1, P1-P3)
All in `MazesEndProvider`, engine untouched, each a heurarm lever DEFAULT ON (`cd5d47686`); units in
`test/unit/test_mazes_end_p1d.cpp` (the older menu tests pin the prunes off):
* **B1 `MTG_MAZE_SUMMIT_COUNT`** -- Guild Summit's count menu {rest-of-line, 0, all} (U <= 1 keeps {0, all}). The
  rest-of-line count rides the UNPINNED cast (chosen_x 0) and is computed at resolution by
  `EtbTapSubtypeDrawDefault`: untapped Gates minus what the rest of the applying line owes (`g_line_unpaid_cost`, plus
  Maze's End's {3} when an untapped Maze's End is ours and the hold carries no activations), net of floating mana and
  untapped non-Gate lands. It is ALSO the beyond-horizon / unpinned default now (decision 8 was "all") -- a playout
  policy change bundled with the prune. The hold covers casts only, so the Maze's End reserve is a board heuristic.
* **P1 `MTG_MAZE_ONE_COLOUR`** -- one colour: no green source on our battlefield -> G; else no blue source -> U; else
  G; neither admitted -> the first admitted. A source = a permanent's own colours (`EffectiveProducesFor`, locked
  CLB / Thran Portal colours included); filters (Planar Nexus, Heap Gate, Talon Gates) and Gond Gate's reflection
  are not sources (default taken).
* **P2 `MTG_MAZE_SPEND_NEVERCAST`** -- the spendable-colour premise skips `NeverCast` cards (Gates Ablaze), so G/U.
  Gates Ablaze never autonomous: VERIFIED (`NeverCast`, unchanged since I1).
* **P3 `MTG_MAZE_LAND_TARGETS`** -- the USER's menu (coordinator reading above). Defaults taken: the Amulet counts
  on the battlefield OR in hand; the new-name Gate is not-in-hand first, then FEWEST copies left in the library
  (singleton), then usable on entry, then the higher ManaSourceRank; an empty menu falls back to every name.
  Burgeoning / Manabond are NOT the same decision (they put lands from HAND, a which-land choice, not a search) --
  listed under P4.

**Held-out A/B** (`build/scratch/maze/p1d/ab1`; seeds d3 b10 94001+gi, d5 b10 95001+gi, d0 93001+gi, 60 games
each; 6 arms innermost in ONE pooled batch, 8 workers -- MEMORY exception, peak RSS 15.4 GB, 45 min; W = 0 then):

| shape | arm | avg win turn | wins <= T8 | ms/game | units/game | vs base (better/worse) |
|---|---|---|---|---|---|---|
| d3 b10 | base (all off) | 6.7500 | 57 | 50,936 | 1,391,578 | |
| d3 b10 | B1 | **6.4833** | 58 | 46,814 | 1,243,012 | 18/3 |
| d3 b10 | P1 | 6.6500 | 56 | 35,780 | 1,014,769 | 10/4 |
| d3 b10 | P2 | 6.7167 | 56 | 39,510 | 1,186,743 | 5/3 |
| d3 b10 | P3 | 6.9500 | 52 | 8,910 | 218,733 | 9/16 |
| d3 b10 | **all** | **6.3667** | **58** | **5,504** | **134,942** | **25/3** |
| d5 b10 | base | 6.5500 | 58 | 35,804 | 1,085,939 | |
| d5 b10 | B1 | 6.3167 | 59 | 32,779 | 959,079 | 13/0 |
| d5 b10 | P1 | 6.4333 | 59 | 27,466 | 868,056 | 9/4 |
| d5 b10 | P2 | 6.4833 | 59 | 31,828 | 979,533 | 5/2 |
| d5 b10 | P3 | 6.6333 | 57 | 6,962 | 194,908 | 11/15 |
| d5 b10 | **all** | **6.2167** | 59 | **4,934** | **130,107** | **20/7** |
| d0 | base | 7.3167 | 45 | | | |
| d0 | B1 / P1 / P2 | 7.3333 / 7.3333 / 7.2500 | 45 / 47 / 46 | | | 7/5, 4/6, 7/3 |
| d0 | P3 / all | 7.0667 / 7.1833 | 48 / 46 | | | 14/6, 16/10 |

All four together: -0.383 turns at d3, -0.333 at d5, ~9x / ~7x cheaper. B1, P1, P2 each better alone. **P3 alone is
WORSE** at d3 (+0.200, 52 vs 57 wins) and d5 (+0.083) though ~6x cheaper and better at d0; together with the others
it is part of the best arm. Root cause (replays in `p1d/diag/`): **d3 seed 94035 gi34** -- base: T5 Sylvan Scrying
finds **Talon Gates of Madara**, put from hand for {4} the same turn (NOT a land play), win T6; P3 alone: Baldur's Gate
is already out but Gond Gate is not held, so the menu is {Gond Gate} (the new-name slot waits for Gond), Gond takes
T5's land drop, win T8. The rule cannot see that Talon Gates is a new name that does not compete for the drop.
Other P3-worse replays (d5 95043 gi42, d3 94012 gi11, 94030 gi29) diverge on turn 2-3 search choices (Manabond vs
Guild Summit, which land first) with no single menu cause visible: search-value differences under the narrower
tree. **Proposed amendment (USER call), lever `MTG_MAZE_TALON_TARGET` (DEFAULT OFF):** a land tutor also offers Talon
Gates while it is in the library and not held -- measured below. P3 stays ON (the "all" arm is the best arm).

**Second pooled batch (`p1d/ab2`, binary with W = 1 default + the two new levers; NEW held-out seeds d3 96001+gi,
d5 97001+gi, 60 games; d1 b1 98001+gi and d1 b3 98101+gi, 30 games; 8 workers, 5.8 min, peak RSS 2.4 GB):**

| shape | arm | avg win turn | wins <= T8 | ms/game | units/game | vs ship (better/worse) |
|---|---|---|---|---|---|---|
| d3 b10 | **ship** (all 4 prunes, W = 1) | 6.2000 | 59 | **1,794** | 150,254 | |
| d3 b10 | W = 0 | 6.1667 | 59 | 2,868 | 103,953 | 5/3 |
| d3 b10 | + ACTIVATE_FIRST | 6.2333 | 59 | 1,831 | 153,115 | 0/2 |
| d3 b10 | + TALON_TARGET | **6.1667** | 59 | 1,908 | 165,036 | 3/1 |
| d3 b10 | P3 alone, W = 0 | 6.6833 | 58 | 5,701 | 189,509 | 1/23 |
| d3 b10 | P3 + TALON alone, W = 0 | 6.6833 | 58 | 5,941 | 200,470 | (vs P3 alone 4/2) |
| d5 b10 | **ship** | 6.3000 | 58 | **1,170** | 95,891 | |
| d5 b10 | W = 0 | 6.3167 | 58 | 2,132 | 68,846 | 2/3 |
| d5 b10 | + ACTIVATE_FIRST | 6.3000 | 58 | 1,164 | 95,897 | 1/1 |
| d5 b10 | + TALON_TARGET | **6.2500** | 58 | 1,238 | 101,295 | 3/0 |
| d5 b10 | P3 alone, W = 0 | 6.7833 | 55 | 3,474 | 99,703 | 1/23 |
| d5 b10 | P3 + TALON alone, W = 0 | 6.6833 | 56 | 3,273 | 99,636 | (vs P3 alone 4/0) |
| d1 b1 (mulligan gen) | ship (W = 1) | 6.3000 | 28/30 | **733** | 63,767 | |
| d1 b1 | W = 0 | 6.3667 | 28/30 | 1,148 | 36,165 | 2/4 |
| d1 b3 (mulligan gen) | ship (W = 1) | 6.3333 | 30/30 | **3,077** | 229,750 | |
| d1 b3 | W = 0 | 6.2667 | 30/30 | 5,039 | 136,742 | 6/3 |

* **Cost vs the 3x rule (15,510 ms/game):** d3 b10 1.8 s and d5 b10 1.2 s per game at the shipped shape (8-worker
  contention; not yet `suite_gate.py --cost`, which needs suite cases). Both well under; was 30-51 s (d3) and 12-36 s
  (d5) before the prunes.
* **W = 1 (USER A3) is now the default** (`MazesEndProvider::SolveChargeWeightOptIn` = 1; levers kept:
  `MTG_SOLVE_CHARGE_W`, heurarm `MTG_SOLVE_CHARGE` = false for the uncharged control). On the pruned tree W = 1 is
  ~1.6-1.8x cheaper at every shape and within noise on win turn (d3 +0.033, d5 -0.017, d1 b1 -0.067, d1 b3 +0.067).
* **d1 (mulligan generation): the uncharged greedy walk dominates.** At W = 0 the walk is unbilled: d1 b1 spends 1,148
  ms on 36K units (31.7 us/unit) against W = 1's 733 ms on 64K units (11.5 us/unit); ~28K of W = 1's units are walk
  visits now billed. Read: about two thirds of a W = 0 d1 game's wall is the greedy walk.
* **Item 5, `MTG_MAZE_ACTIVATE_FIRST` (USER A4, a move-order tier, DEFAULT OFF): no gain** (d3 0/2 worse, d5 1/1).
  Beyond the horizon it is already covered: the playout's 200 new-name fetch eval takes the activation whenever no
  land drop + new-name Gate remain (the P1c deferral). Recommendation: leave OFF (the USER's call).
* **P3 amendment `MTG_MAZE_TALON_TARGET` (DEFAULT OFF, USER call): better on top of ship** (d3 -0.033 3/1, d5 -0.050
  3/0, +6% cost) and on top of P3 alone (d5 -0.100, 4/0). Recommendation: adopt.

(ms in this second batch are CONTENDED: another agent's 8-worker batch started at ~01:59 UTC and overlapped it.
Quality and units stand; the cost verdict is re-timed below.)

**Cost re-timed UNCONTENDED** (`p1d/retime`, 02:46 UTC, only P1c's single b0 game alongside; Release binary =
MTG_PGO=0; ship arm, the ab2 seeds; 8 workers, 58 s, peak RSS 1.34 GB; all 180 digests and units identical to ab2):

| shape | ms/game | units/game | avg win turn | vs 15,510 ms (3x Hinata 5,170) |
|---|---|---|---|---|
| d3 b10 | 1,789 | 150,254 | 6.2000 | 0.12x -- passes |
| d5 b10 (play) | 1,086 | 95,891 | 6.3000 | 0.07x -- passes |
| d1 b1 (mulligan gen) | 712 | 63,767 | 6.3000 | |
| d1 b3 (mulligan gen) | 2,839 | 229,750 | 6.3333 | |

(Not `suite_gate.py --cost`, which needs suite cases; a pooled batch at the suite shapes, 8 workers for memory.)
Units on this tree: **3429/3429** (`p1d/units2.log`). Smoke NOT run (the changes are `MazesEndProvider`-only plus
heurarm slots appended at the end; other decks expected byte-identical). The P1c d3 b0 game (seed 91005 gi4) was
still running at ~2 h 45 min when P1d stopped -- see `logs/maze/P1d-handoff.md`.

**Item 3 -- "d3 is weirdly expensive" (USER A5): it is not; the census sets were different games.** On the SAME
seeds, d3 b10 and d5 b10 play byte-identical games: 60/60 digests on seeds 96001+gi with the shipped tree
(`p1d/pair1`: both 150K units/game, 6.200) and 16/16 on seeds 92001+gi with every prune off and W = 0 (`p1d/pair2`:
both 772,743 units/game, 6.625). At a 10 ms budget (~9,000 units per decision, with the 25x overrun ceiling) the
search spends its allowance before any line reaches a fourth turn, so `--depth 5` never binds and the two rows run
one search. Every "d3 costs more than d5" reading (P1: 109 vs 54 s/game; P1b/P1c: 30 vs 13 s) compared d3 on the
91001 seed set with d5 on the 92001 set -- and 91001 holds the heavy games (91021 gi20 alone was 7.7 s of P1b's
30.5 s/game). Re-attribution of 91021 gi20 after the prunes (W = 1, census `p1d/diag/c91021_d3.tsv`, single thread,
instrumented): 22 s wall (was 432-465 s), 163 decisions, median 11,680 units, max 42,275 (4x the median -- no tail
decision left); units by site greedy_walk 45.6% (billed now, W = 1), la_cand 20.2%, rollout_step 15.6%,
greedy_fallback 15.0%; option groups never wider than 3. Consequence for the suite: a d3 b10 row adds no coverage
over a d5 b10 row for this deck at this budget (they are the same game) -- the USER's call whether to keep both.

**Item 4 -- memory.** jemalloc heap profiles (Profile build, `MTG_POOL_ALLOC=0`, seed 91021 gi20 d3 b10 single
thread; `p1d/diag/heap*`):
* PRE-prune (all four off, W = 0): peak RSS 1.60 GB; sampled live heap at the peak 1.36 GB, 99% under the mulligan
  BOTTOMING rollouts (`BottomCards` -> `RolloutWinTurnFrom` -> `FullSearchLine` -> `FSLineTail`). Self: the mana
  cache's tier-1 store `McTier1Insert` 434 MB (tier 1 + its tier-2 spill share the allocation site); the plan COPIES
  of the sub-decision axis fan (`AppendSubdecisionAxes` / `PbAxesAndTwins` -> `TurnSolver::Plan::Plan`, 373 MB, plus
  160 MB of `reserve`); `TapForCostBacktrackTop` self 170 MB.
* SHIPPED (prunes on): peak RSS 218 MB; live heap 158 MB (mana cache 53 MB, payment 20 MB, plan copies 15 MB).
* What is unbounded: the per-enumeration plan fan -- every sub-decision axis variant of every plan is
  MATERIALISED as a full `Plan` copy (a vector of 496-byte `Action`s), so the transient is the PRODUCT of the axis
  widths (Summit count x colours x tutor targets x ...). The USER's prunes collapse exactly those axes, which is why
  the heavy game went 1.6 GB -> 0.22 GB and the 8-worker batch peak 15.4 GB -> 2.4 GB. The mana caches are bounded
  (tier 1 24 MB portable per thread, tier 2 256 MB per process) but their REAL bytes run ~1.5x the portable count.
  At the shipped settings a 32-worker tier projects to ~7-10 GB, under the 16 GB cap. A structural bound for the
  general case (generate the axis variants lazily instead of copying every Plan) is an ENGINE change, proposed, not
  built.

**Item 7 -- A1 weight sweep** (heuristic-optimization; TEMPORARY selector heurarm `MTG_MAZE_WSEL_0..2`, 8 variants of
(per-name, Maze's End held, per new-name Gate in hand, fetch eval): v0 shipped (2M, 0.5M, 0.1M, 200), v1 1M/name,
v2 4M/name, v3 names only (0 held, 0 hand), v4 (2M, 1M, 0.3M), v5 fetch 100, v6 fetch 400, v7 (2M, 1.5M, 0.5M)).
TRAIN (`p1d/wtrain`, d5 b10 99001+gi x100, d1 b1 99501+gi x60): v1, v2, v4, v7 play (near-)identical games to v0;
v3 best (d1 b1 6.267 vs 6.400, 7/0; d5 6.41 vs 6.43, 5/5), v5 next (d5 6.41, 6/4). HELD-OUT (`p1d/wval`, d5 b10
99801+gi x300, d1 b1 99601+gi x100): **v3 does NOT validate** (d5 6.363 vs 6.350, 11/15; d1 b1 6.34 vs 6.33, 8/10);
**v5 (fetch eval 100) is slightly better** (d5 6.337 vs 6.350, 13/10; d1 b1 6.26 vs 6.33, 9/2). On the pruned tree
the weights barely move play. Default unchanged (v0) pending the USER; the selector is to be deleted once the USER
picks (ms contended; units comparable).

**Item 8 / P4 -- further prune candidates (REPORTED, NOT BUILT; each needs a USER ruling).** From the shipped-tree
census of 91021 gi20 (`p1d/diag/c91021_d5`: 837K odometer positions, 435K plans; option groups of width 1-3 only)
and the menus as built:
1. **Maze's End's fail-to-find twin** (+1 option on every activation): with any legal Gate target, "find nothing"
   looks DOMINATED in this list -- the find only adds a land (and a Guild Summit draw); nothing here rewards a
   smaller board or a bigger library. Deck-scoped dominance, sound by the same argument as decision 28.
2. **Interchangeable hand copies** (engine-wide, priced by the census, not applied): each duplicate hand copy is
   its own x2 group; folding them (C(s+k,k) for (1+s)^k) prices **1.79x** fewer odometer positions over this game
   (Exploration, Burgeoning, Amulet, Manabond, Sea/Manor/Cliffgate, Gond Gate copies). An identity fold, but an
   ENGINE change for every deck.
3. **Burgeoning's land menu** (decision 23: every distinct land name in hand x its entry pins, + none): the P1 colour
   rule already narrows the pins; the land itself could follow P3's logic (a new-name Gate first, a duplicate name
   last) -- width = distinct lands in hand + 1 (typically 2-4).
4. **Archdruid's Charm mode 1: to hand vs onto the battlefield tapped** for a land (x2 per Charm find): with no land
   drop left, "battlefield" dominates "hand"; with a drop open and the land entering tapped anyway, "hand" keeps the
   option -- a conditional prune.
5. **Gateway Plaza pay-or-sacrifice** (x2): sacrificing loses a land and a Gate name; dominated unless the {1} buys
   a cast this turn -- a conditional prune.
6. **Heap Gate's Treasure** (x2 per activation, searched): converts a Gate tap into a Treasure; worth it only when
   the Treasure's colour or the next turn's mana is the binding constraint.
7. **Exploration multi-drop land ORDER**: two enters-tapped Gates in either order reach the same board (an identity
   fold the census could price with a land-order counter; not measured).
B1/P1-P3's menus are already 1-3 wide, so the remaining width is in plan SUBSETS rather than per-decision menus.

### Memory (new finding, P1b)
A heavy Maze's End d3 game holds ~1.5-2+ GB of transient RSS beyond the bounded pools (the pools read fsl 0 /
plan 512M / t2 256M at the cap), so a census batch at 32, 20, 12 and 8 workers with the heaviest seeds first
tripped the 16 GB `MTG_RSS_CAP_GB` within 20 s-5 min. What ran: 8-16 workers in natural order with
`same_seed_inflight` 1-2 (peak 13.6 GB at 8 workers; s3 peaked 16.3 GB at 10 workers but finished). Worker count
was reduced for MEMORY (coordinator rule: workers x peak RSS < ~18 GB), recorded here per the CPU rule. NOTE for
suite admission: the regression tiers pool 32 workers; Maze's End rows there could hit the same wall. Also seen:
`MemBudget`'s split assumes the affinity count (32) even under `--threads N`.

## S4 (2026-11-18): Talon default, smoke, Stage 4

### Coordinator defaults (each a default, USER sign-off pending)
* **P3 amendment `MTG_MAZE_TALON_TARGET` -> DEFAULT ON** (`94fda7ec6`). Measured better on top of the shipped prunes
  (P1d ab2: d3 -0.033 3/1, d5 -0.050 3/0, +6% cost). Unit "P3 amendment, MTG_MAZE_TALON_TARGET default ON" pins the
  menu with the flag on/off, Talon in hand, Talon controlled, and no Maze's End held ({Maze's End} alone still wins).
* **`MTG_MAZE_ACTIVATE_FIRST` stays OFF** (P1d: no gain, d3 0/2, d5 1/1).
* **A1 weights stay v0** (2M per name, 0.5M Maze's End held, 0.1M per new-name Gate in hand, fetch eval 200); the
  temporary `MTG_MAZE_WSEL_*` selector stays until the USER picks (v5, fetch eval 100, was slightly better held-out).
* **B1's playout-default change accepted** (Guild Summit's beyond-horizon / unpinned count = the rest-of-line count,
  was "all").
* **No P4 candidate built** (the seven P1d item-8 candidates stay reported, each awaiting a USER ruling).
* **The suite gets the standard ENTRY set** (below), with a d3 b10 sanity row and d5 play rows at b20.

### Units and smoke (Talon ON)
Units **3430/3430** (`build/scratch/maze/s4/units1.log`). Smoke (Release, MTG_PGO=0): **all 219 cases byte-identical
to `smoke-p1b`** (every `.wins` file, per-game digests included; saved `build/scratch/maze/smoke-s4/wins`).
168 pass / 51 fail = the 44 stale GT + the 7 known tie-fix moves (USER still deciding on those). GT NOT accepted.

### Stage 4 -- baseline profile (`analyze_deck.py --no-rebuild --analyzer-seed 1`)
Wrote `decks/Maze's End/Maze's End.profile.json` (14 min wall, peak RSS 5.2 GB; the cost-diagnostic batch would have
used min(20, cpu_count) workers -- capped at 8 by the coordinator's memory rule via a cpu_count shim, but it did not
run, see below). The seed was pinned (1) so the profile is reproducible; the skill's recipe leaves it unseeded.
* **Mulligan baseline:** `hand_score_threshold` -1e18 (keep on score, as 49 other profiles); static
  `mulligan` = {min_lands 1, **max_lands 5**, min_playable 0, stop_at 4, curve_check two_drop, bottom_order
  count_first, no required pieces}; `mulligan_flags` empty. NOTE for the USER (mulligan policy is theirs): the list
  runs 35 lands, so the generic `max_lands 5` sends roughly one 7-card hand in ten (6-7 lands) back. This is the
  analyzer's generic default, not a Maze's End judgement; the exhaustive keep table (mulligan-profile stage) replaces it.
* **card_scores:** noisy at this sample. Interchangeable Guildgates range from Boros -0.243 / Orzhov -0.207 to
  Golgari +0.215 -- the spread is sampling noise, not a card difference. Gates Ablaze -0.420 (never cast), Guild
  Summit +0.343 / -0.345 (first / second copy), Exploration's second copy -0.590. Scores are a keep-time prior only.
* **cost_diagnostic:** `NO_COST_INTERACTIONS` (no same-turn cost-mechanic cards), so no games ran.
* **discard_analysis:** `STATUS_QUO_OK` (400 evidence games, 67 cleanup-discard decisions, all multi-optimal: the
  current ranking is at the searched optimum; no derived order).
* **fetch_hold_review:** not required. **dependency_map:** no edges.

### 4a -- provider routing
`provider_audit.py "decks/Maze's End"`: **MazesEnd** (its own provider, routed first in `SelectDecisionProvider`),
certificate `NotAssessed` with its stated proof obligation (no line reaches ten Gate names this turn: land drops,
Exploration/Burgeoning extras, Maze's End bounce-replay loops with Amulet, Guild Summit/tutor digs, mana per {3}).
`--check` over every deck: exit 0; only the standing name-mismatch notes (Eldrazi Displacer Flicker ->
EldraziFlicker, Slivers -> Vial).

## Regression suite (4-bis / 5j, S4 2026-11-18)

**Cost gate (`suite_gate.py --cost`, ONE pooled regression run of mazesend + hinata + hinata2hg, Release, harness
default 32 workers, only P1c's single b0 game alongside):** mazesend **6,914 ms/game** (worst searched shape
d5 b20, seeds 2002+3003 pooled; ~255K units/game) vs Hinata **4,769 ms/game** = **1.45x -- PASS** (limit 3x,
15,510 at P1b's yardstick, 14,307 at this run's). Total 2,358 core-s over 8 jobs. (Under a 32-worker tier the
per-unit cost is ~2.3x P1d's 8-worker re-timing, so 6.9 s/game here vs 1.1 s at d5 b10 there is load + b20, not
a regression: units/game 255K at b20 vs 96K at b10.)

**Rows added (all three tiers, ENTRY set, Hinata's shape; key `mazesend`; no value_play block -> d5 rows = play
d5 b20):**
* SMOKE: d0 1000, d3 50 b10, d5 100 b20, `mazesend2hg` d3 50 b10 (the small 2HG canary: no card reads heads or
  starting life -- Burgeoning's passive opponent plays one land per turn, Gates Ablaze is never cast; its digest
  equals the 1-head d3 row's, as expected).
* REGRESSION: d0 2002 x1000, d3 2002 x75 b10, d5 2002/3003 x150 b20, plus the unbudgeted backstop.
* OVERNIGHT: d0 4x2000, d3 4004 x125 b10, d5 4004-7007 4x300 b20, plus the unbudgeted backstop. NOT run and NOT
  accepted here (the overnight tier is the periodic run); its 13 keys are NEW until that run accepts them.

**Unbudgeted backstop -- budget-0 play is DEGENERATE on this deck (new finding).** Screened 28 blocks
(30001..30541 step 20; 10 games for 30001..30181, 3 for the rest; d3 + d5; abandon_units 2,000,000;
`build/scratch/maze/s4/bscreen{,2}`): **171 of 246 games hit the 2M-unit cap** (12-110 s each, 8 workers; 2.5 GB
peak RSS). No block's first TWO games finish at both depths, so every backstop row is ONE game (a block's first
game -- the only sub-block whose seed and game_index match the screen): regression 30121 and 30081 (30081 gi0 d3
1.10M units, 20.9 s in the 32-worker tier), overnight 30341 and 30441. This matches P1c's d3 b0 game (seed 91005
gi4), still running after 3.5 h. It is a performance finding for the unbudgeted paths, not a suite blocker (the
backstop smokes the paths; it does not measure quality) -- surfaced to the USER.

**Tier results (Release, MTG_PGO=0):**

| key | avg win turn / digest | ms/game | units/game |
|---|---|---|---|
| mazesend_smoke_d0_s1001 | 7.4050 / b8e19b29 | 1 | - |
| mazesend_smoke_d3_s1001 | 6.4600 / 35415b86 | 5,116 | 184K |
| mazesend_smoke_d5_s1001 | 6.3200 / e95ebd31 | 5,948 | 230K |
| mazesend2hg_smoke_d3_s1001 | 6.4600 / 35415b86 | 4,776 | 184K |
| mazesend_regression_d0_s2002 | 7.4490 / e3af3e5e | 1 | - |
| mazesend_regression_d3_s2002 | 6.2933 / 131ce989 | 3,379 | 145K |
| mazesend_regression_d5_s2002 | 6.2333 / cb2b46a6 | 7,022 | 252K |
| mazesend_regression_d5_s3003 | 6.3667 / 4e9ef361 | 6,805 | 259K |
| backstop d3/d5 s30121 | 5.0000 / 5.0000 | 7,620 / 649 | 358K / 16K |
| backstop d3/d5 s30081 | 6.0000 / 5.0000 | 20,913 / 819 | 1.10M / 17K |

Accepted with the deck-scoped accepts (`--smoke --accept --deck=mazesend,mazesend2hg`, `--accept --deck=mazesend`):
the GT diff is exactly the 12 new keys plus the header line; `check_gt_logs.py` 1264 consistent, 0 stale.
`job_cost.tsv` keeps only the mazesend entries (Hinata's re-recorded costs reverted). **Something else moved:**
`hinata_regression_d5_s2002` and `_s3003` FAIL digest-only (one game each, gi122 / gi128, same win turn) -- NOT
this branch: their new digests equal the GT already on `origin/phase-1-2-deck-analyzer` (f71a7e0ce reconciled
them); this branch's base GT predates that. Not accepted here.

## Approved deferrals

* **Decision 9, Archdruid's Charm mode 2** (target creature we control; the list has none) -- USER B3 2026-11-17: "I
guess it can be deferred. The only mode we care about heuristically is the first."
* **Decision 33, Talon Gates' ETB phase-out** -- USER B4 2026-11-17: "Okay, I guess we can wait on that."

## Verification

(Stage 5 verdicts)

## Sweep fixes (F1, 2026-11-18; Stage 5d sweep seed 61001)

Full write-up: `logs/maze/F1-results.md`. Branch `wip/maze-fix`.
- **gi6 (engine bug, FIXED bd32848c7):** a search-put's draw triggers (Guild Summit, Kili, Puresteel, Vaultborn,
  Enduring Innocence, ETB self-draws) now wait for the ability's "then shuffle" (CR 603.3; SearchPutTriggerScope).
- **gi1 (completeness, FIXED 8e2d4100e + e5daf237b):** Talon's put credits its {C}, so "put Talon + activate Maze's
  End" is enumerated; plans hold Maze's End's {T} source; and (F2) the trailing pass pays a Channel-kind step with an
  owed-aware hold (TrailingStepOwedHold) when the usual payment would strand a later activation -- Talon's {4} is paid
  through Baldur's Gate's fed mode and the {3} stays payable. Both worlds.
- **gi2 (completeness, FIXED 5ea2d6be7):** site 10 DID arm after a same-plan Exploration, but MTG_BP_NEW_ONLY emptied
  its continuation list (every land in hand is "old", yet no base plan plays two lands). A land play after this
  turn's first land now counts as new. T2 Selesnya + Exploration + Izzet + Sylvan Scrying; gi2 T6 -> T5.
- **gi8 (rules-modelling gap, PARTLY MODELLED 8e2d4100e):** Maze's End's activation has no timing restriction. A
  WINNING off-main activation (our end step after Manabond, the opponent's turn after Burgeoning) is now taken
  (dominance). DISCLOSURE (USER sign-off pending): a NON-winning off-main activation is not modelled; it needs a
  searched end-step frame (design in F1-results).
