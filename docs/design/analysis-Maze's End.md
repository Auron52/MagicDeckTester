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

## Claude-play sweep

Recorded by V1 from the coordinator's report (2026-11-18). The sweep (analyze-deck 5d) was run by the coordinator.

- commit: `dab956a9d`
- seeds: base 61001
- games: 16 (gi 0-15)
- driver: Claude (Opus) agents, one per game. A second agent adversarially verified each flag.
- flags: 4 unresolved. Each is an OPEN DEFECT owned by agent F1 (branch `wip/maze-fix`, worktree
  `.claude/worktrees/maze-fix`). None is a deferral.
  1. gi6, engine: Guild Summit's Gate-enter draw resolves before Maze's End's "then shuffle", against CR 603.3. The
     player draws the known pre-shuffle top card.
  2. gi1, search completeness: the Talon Gates {4} put + Maze's End activation combination is never enumerated
     (`legal_not_enumerated` at d8 b0).
  3. gi2, search completeness: the extra land drop after a same-plan Exploration cast is unreachable at T2 at d8 b0.
  4. gi8, rules gap: there is no instant-speed window for the Maze's End activation (end step after Manabond, the
     opponent's turn after Burgeoning). It cost a T6 win.
- 3 budget-bound search issues (gi0, gi7, gi13): the d5 b20 line is one turn slower, and b100-b500 finds the turn
  earlier. This is starvation evidence for 5c (see Verification).
- 6 flags dismissed as false positives, with reasons in the summary.
- Per-game ai/claude win turns: gi0 7/6, gi1 7/8, gi2 6/5, gi3 6/6, gi4 5/5, gi5 7/5 (the mulligan of a T5 hand,
  `max_lands`), gi6 5/5, gi7 6/5, gi8 7/7, gi9 8/8, gi10 5/5, gi11 5/6, gi12 5/8, gi13 7/6, gi14 5/5, gi15 7/6.
- Full data: `/workspaces/MagicDeckTester/logs/maze/sweep/sweep-61001-summary.txt` and `sweep-61001-full.json`.

## Approved deferrals

* **Decision 9, Archdruid's Charm mode 2** (target creature we control; the list has none) -- USER B3 2026-11-17: "I
guess it can be deferred. The only mode we care about heuristically is the first."
* **Decision 33, Talon Gates' ETB phase-out** -- USER B4 2026-11-17: "Okay, I guess we can wait on that."

## Verification

(Stage 5 verdicts)

### Stage 5 (V1, 2026-11-18): everything except the claude-play sweep

Tree `dab956a9d` (src) = the binary `build/scratch/maze/v1/mtg.v1` (Release, MTG_PGO=0). Scratch:
`/workspaces/MagicDeckTester/build/scratch/maze/v1/`. Every batch ran with 8 workers, `MTG_RSS_CAP_GB=16` and
`MTG_MEM_BUDGET_MB=8000`. The worker count is a MEMORY exception (the coordinator's rule, with the claude-play sweep
and F1's 12-job builds sharing the box). A guard (`tools/memguard.sh`) paused a batch with SIGSTOP while available
memory was under 4 GB and resumed it at 7 GB. The budget is in units, so a pause does not change any game.

**verify_deck.py.** The static pre-check (`--no-sweep`) and the full run (25.8 min, `taskset -c 0-7`) give:
coverage, card_costs, card_fields, viewer_wiring (choose_color, dig, land_entry, target, x_value), mismatch,
play_invariants (8 games, 458 decisions) and suite all PASS. Two gates FAIL:
* `regression_tiers`: the overnight tier has no GT. This is NOT a deferral and is never signed off. The periodic
  overnight run accepts the 13 `mazesend` overnight keys.
* `viewer`: a TOOL bug, now fixed (`9ffcb7820`). `audit_viewer_decisions.py` bound `unreached` only inside the
  hard-miss branch, so a sweep with NO hard miss crashed with UnboundLocalError (rc=1). The sweep itself was clean:
  observed {choose_color, dig, discard, land_entry, target, tutor_etb, x_value}, which includes all 5 expected types.
  The same bug is on `origin/phase-1-2-deck-analyzer`.
The 709 `oracle_text advisory` lines cover the whole DB, not just this deck. They come from bracket notes appended to
the oracle text, and they are advisory only.

**5a, mismatch harnesses.** The verify_deck default seeds (7001/8001 x 60, d3 b20 nonconv / d5 b20 fd) found 0
lines, but 7007-7060 overlaps the overnight `s7007` row. So I re-ran both harnesses on seeds disjoint from the suite:
120001 and 121001 (100 games each, at d3 b10 and d5 b20) and 111001 (150 games, at d3 b20 and d5 b20). That is 700
games per arm (`mm_nonconv/`, `mm_fd/`).
* `[nonconv]`: **0**.
* `[fd-diverge]`: **1**, seed 121065 gi64 d5 b20, `realized_win=7 predicted_win=6 proven_at_turn=6 leaf_est=none`.
  ROOT-CAUSED as a rollout-vs-executor mismatch: the line is verified by simulation and the leaf is not involved.
  * The committed T6 line (`MTG_FD_TRACE`) is: Gruul Guildgate + Sylvan Scrying (finds Talon Gates of Madara) + Amulet
    of Vigor, with the breakpoint continuation `[Orzhov Guildgate, Exploration, Maze's End activation, Talon Gates
    {4} put]`.
  * In the executor, Sylvan Scrying's {1}{G} was paid through Baldur's Gate's `{2},{T}: X` filter (float g6). The
    `{2}` came from Maze's End's own `{C}`. `MTG_EXEC_TAP_TRACE` "before Sylvan Scrying" lists neither Baldur's Gate
    nor Maze's End as untapped.
  * So when the replayed continuation reached the activation, Maze's End was already tapped:
    `MTG_ACT_DROP_AUDIT=2`: `noTap`. Talon was put and the T6 win slipped to T7.
  * `MTG_LINE_PAY_AUDIT` reports REAL `short_plans=0`. The first segment's payment holds nothing for an activation
    that lives in the recorded breakpoint continuation, so the audit cannot see this loss.
  * Same seam as the sweep's gi1 flag (Talon put + Maze's End activation): **OPEN DEFECT, proposed owner F1**
    (`wip/maze-fix`). V1 did not edit those sites.
  * Repro: `tools/one.sh <bin> <out> 121065 64 5 20 MTG_FD_ORACLE=1 MTG_FD_TRACE=1 MTG_EXEC_TAP_TRACE=1`.

**5b, multi-depth sanity (play budget b20, seeds 111001+gi, 150 games each, per-game jobs; `main/`).**

| depth | avg win turn (loss = 9) | wins <= T8 | ms/game | units/game |
|---|---|---|---|---|
| d0 | 7.2933 | 111/150 | 2 | - |
| d3 b20 | 6.1933 | 147/150 | 3,916 | 227K |
| d5 b20 (play) | 6.1933 | 147/150 | 4,127 | 232K |

* **d3 = d5: 149/149 paired games have identical digests**, the P1d item-3 finding at b20. The budget is spent before
  any line reaches a fourth turn, so `--depth 5` never binds at the play budget. Monotone, trivially.
* **d0 vs d5:** d0 is slower in 97 games, equal in 48 and FASTER in 4 (gi33 7 vs 8, gi49 6 vs 7, gi122 5 vs 6,
  gi124 7 vs 8). Read gi49: at T2 the search plays Thran Portal + casts Exploration but NOT the extra drop (d0 plays
  Sea Gate too), and at T3 it plays one land with two drops open. This is the sweep's gi2 class (an extra drop after
  a same-plan Exploration cast, F1's defect) or budget. The 5c ladder classifies all four.
* **Plausibility:** T6.2 at play settings, with 2% losses (3/150: gi28, gi43, gi104). For a ten-name alternate win in
  a 35-land list that is plausible: the sweep's Claude games land T5-T8. The suite GT agrees (d5 s2002 6.23, s3003
  6.37).
* **Budget b20 -> b200 (40 games, d5):** 5.9250 vs 6.1500 on the same 40 seeds. 8 better, 1 worse (gi16 6 -> 7).
  Read gi16: the two budgets diverge at T3. b20 makes a Heap Gate Treasure and holds Burgeoning/Exploration; b200
  casts both. The b200 line then shuffles (Archdruid's Charm T5, Maze's End T6), so its later draws are not the ones
  its simulation saw. A shuffle-variance outlier, not a defect.

**5e / 5f, the provider heuristics against the oracle (d5 b20, the same 100 games, arms innermost; `main/`).**
Leave-one-out from the shipped tree (each USER-directed prune switched off alone), the all-off arm (B1, P1, P2, P3
and Talon off), and the all-off arm at 10x budget:

| arm | avg | wins <= T8 | ms/game | units/game | vs ship: better / worse / same digest | net turns |
|---|---|---|---|---|---|---|
| ship | 6.1500 | 98 | 3,242 | 197K | | |
| no B1 (Summit count + its playout default) | 6.6700 | 94 | 3,529 | 217K | 0 / 39 / 36 | +52 |
| no P1 (one colour) | 6.1500 | 98 | 3,490 | 209K | 5 / 4 / 61 | 0 |
| no P2 (Gates Ablaze not spendable) | 6.1500 | 98 | 3,232 | 197K | 0 / 0 / **100** | 0 |
| no P3 (land-target menu) | 6.1600 | 97 | 19,891 | 899K | 13 / 14 / 10 | +1 |
| no Talon amendment | 6.2100 | 98 | 7,139 | 192K | 1 / 7 / 77 | +6 |
| all off | 6.8100 | 91 | 48,417 | 1,333K | 5 / 49 / 3 | +66 |
| ship, b200 (40 games) | 5.9250 | 39/40 | 25,611 | 1,382K | | |
| all off, b200 (40 games) | 6.5500 | 38/40 | 223,809 | 7,329K | 1 / 18 / 3 | +25 |

* **B1 is the large quality term.** Removing it costs +0.52 turns/game and never helps. The arm also reverts the
  playout default (rest-of-line count -> "all"), so the gain is not the in-window prune alone.
* **P1 is quality-neutral (5/4) and about 6% cheaper.**
* **P2 is INERT on top of P1:** 100/100 digests are identical, because P1 already picks G or U. It can matter only
  with P1 off.
* **P3 is quality-neutral (13/14, +1) and its value is cost: 6.1x fewer ms and 4.6x fewer units.**
* **The Talon amendment is better (1/7, +0.06), and without it one game takes 415 s.**
* **Completeness at a larger budget:** even at 10x budget the all-off tree is worse than ship (1 better / 18 worse).
  The one game where it is better is gi18 (ship 7, all-off 6): a candidate line the prunes make harder to find. The
  5c ladder checks whether ship recovers it at b2000.
* These prunes are USER-directed. The numbers are reported here; nothing was removed.
* **The `MTG_UNPRUNED` oracle (the skill's lever) is MEMORY-BLOCKED on this box; 6 games ran.**
  * The lever also opens the provider's sound dominance prunes (Gates Ablaze `NeverCast`, Charm mode 3, the
    spendable-colour premise) and the engine's narrowing gates.
  * Three attempts at d5 b20 (8, 4 and 3 workers) each reached 6-14.6 GB RSS within 3-4 minutes and were killed (V1's
    own runs). At 3 workers it was 14.6 GB, so a single unpruned game exceeds several GB. This is the P1d item-4
    unbounded plan fan: every sub-decision axis variant is materialised as a Plan copy.
  * On the 6 games that finished (gi0-5): unpruned 6,6,6,7,6,9; all-off 6,6,6,6,6,9; ship 6,6,5,6,5,6.
    Unpruned is 0 better / 4 worse vs ship, the same direction as the all-off arm.
  * The full 100-game oracle diff needs a quiet box (no concurrent builds) at 1-2 workers, or the lazy axis
    generation proposed in P1d item 4.

**5c, budget starvation (d5, the shipped tree, seeds 111001+gi; `leaf/` ladder jobs + `main/`).** Each slow game
(T8 or a loss at b20), each game where d0 beat d5, the b200-worse gi16 and the all-off-better gi18 were re-run up the
budget ladder. Win turns (9 = no win by T8):

| gi | d0 | b20 | b100 | b200 | b500 | b2000 | reading |
|---|---|---|---|---|---|---|---|
| 32 | 9 | 8 | 8 | 8 | 7 | 7 | starvation, recovers at b500 |
| 33 | 7 | 8 | 6 | 7 | 7 | 6 | starvation (non-monotone between budgets) |
| 100 | 8 | 8 | 6 | - | 6 | 6 | starvation, recovers at b100 |
| 124 | 7 | 8 | 6 | - | 6 | 6 | starvation |
| 142 | 8 | 8 | 7 | - | 7 | 7 | starvation |
| 145 | 8 | 8 | 8 | - | 7 | 7 | starvation, recovers at b500 |
| 147 | 9 | 8 | 7 | - | 7 | 6 | starvation |
| 18 | 9 | 7 | 7 | 7 | 6 | 6 | ship recovers all-off's T6 at b500, so the prunes do not make it unfindable |
| 16 | 9 | 6 | 7 | 7 | 7 | 6 | the shuffle-variance outlier above; back to T6 at b2000 |
| 49 | 6 | 7 | 7 | - | 7 | 6 | recovers only at b2000. The T2/T3 missed extra drop (F1's gi2 class) |
| 122 | 5 | 6 | 6 | - | 6 | 6 | **NOT recovered at 100x.** d0 wins T5 by playing a second land after a same-plan cast (T3 Gateway Plaza + Exploration + Manor Gate; T4 Sea Gate + Golgari with Exploration out). The search never plays the land after the cast, at any budget. It is F1's gi2 completeness defect (a structural hole), not a budget issue. |
| 28 | 9 | 9 | 9 | 9 | 9 | 9 | draw-bound: no Maze's End or tutor by T8, hand empty from T3. d0 loses too |
| 43 | 9 | 9 | 9 | - | 9 | 9 | draw-bound: Maze's End and Sylvan Scrying first seen on T8 |
| 104 | 9 | 9 | 9 | - | 9 | 9 | draw-bound: the d0 and d5 games are byte-identical (no decision changes it) |
| 78 | 8 | 8 | 8 | - | 8 | 8 | draw-bound: Maze's End first seen T7 |
| 93 | 9 | 8 | 8 | - | 8 | 8 | land-screwed: 3 lands T3-T5. d0 loses |
| 105 | 8 | 8 | 8 | - | 8 | 8 | budget-invariant to 100x. Maze's End seen from T5. d0 is the same |

Reading:
* The play budget b20 STARVES this deck. On these 40 seeds b200 is 0.225 turns/game faster, and 7 of the 13 slow
  games recover at b100-b500. This agrees with the sweep's gi0, gi7 and gi13 (b100-b500 finds the turn earlier).
* The depth setting is inert at b20 (5b). So for this deck the budget, not the depth, is the play-settings lever.
* Raising b is a settings question for the USER (cost: b200 ship is 25.6 s/game against 3.2 s at b20, both at 8
  workers; the suite gate measured 6.9 s/game at b20 under 32 workers).
* No game is lost to the prunes at large budget.

**5g, earliest-win rule mining: UN-RUN (cost; USER call).**
* The probe (`ewin/probe.err`): 2 games at d5 b20 with `MTG_DUMP_EWINS=1 MTG_SEARCH_ORDER=1`, single-threaded,
  took 12.4 min for 12 decisions. That is about 6 core-min/game.
* The skill's sizing (500 games x 2-3 seeds) is therefore about 100-150 core-hours: about 12-19 h at the 8-worker
  memory cap.
* A 24-game scout was queued and then dropped. Other agents' builds held the box at 0-3 GB available, and the
  scout's sample is too small to mine ORDER rules.
* This deck's provider heuristics are USER-directed (B1, P1-P3, Talon). 5e measured them directly against the
  pruned-off and unpruned trees.
* Scheduling a full mining run (an overnight-class job) is the USER's call.

**5c2, the horizon-honest tie-break (`leaf_tiebreak_check.py` at PLAY settings, 6 blocks x 150, both arms pooled in
ONE batch with the 5c ladder; MTG_PGO=0, 8 workers; `leaf/verdict.txt`).** For this deck the arm toggles
`GradesNoWinLeaf`, which carries `MazesEndProvider::NoWinLeafProgress` (the A1 weights). Opponent life never moves
here.

| split | games | net turns | worse | better |
|---|---|---|---|---|
| half A | 450 | -6 | 22 | 27 |
| half B | 450 | -14 | 28 | 38 |
| ALL | 900 | **-20** | 50 | 65 |

Binding 12.8% (115 of 900 changed). base 6.3667, leaf 6.3444. **VERDICT: KEEP THE DEFAULT (ON).** Both halves
agree, so there is no opt-out. The sample is reduced from the script's 12 x 1000 default because 115 changed games
is far above the 20-game sign threshold.

**5h, viewer decision surface.**
* `audit_viewer_decisions.py` (40 games from seed 9001; `viewer_audit.log`) observed every expected type: choose_color
  (CLB Gates, Thran Portal), dig (Gateway Plaza, Burgeoning/Manabond crossings), land_entry (The Black Gate), target
  and x_value (Guild Summit).
* There was no HARD MISS, no UNVERIFIED tail, no self-guard failure and no driver failure. The rc=1 was the tool bug
  fixed in `9ffcb7820`.
* Oracle cross-check: one advisory, Talon Gates' "up to one target creature phases out". It carries its bracket note,
  is the approved deferral (decision 33, USER B4), and is goldfish-inert.
* `viewer_wiring`: all 5 types have an emitter in main.cpp and a GUI branch in index.html. Nothing to wire.

**5i, discard.**
* `STATUS_QUO_OK` is confirmed from the Stage 4 output (`build/scratch/maze/s4/analyze.json`): 400 evidence games,
  67 cleanup-discard decisions, all multi-optimal, mean regret 0.
* The skill still asks for an authored bucket policy, reported to the USER, even under STATUS_QUO_OK. None is
  authored yet. The stakes are tiny (0.17 sheds/game, all ties).
* Listed as an open item for the coordinator and the USER.

**Final `verify_deck.py --write-ledger` (V1, after `9ffcb7820`).** Result: **GATE FAIL**, with exactly two blocking
items. Neither is a deferral, and none was signed off:
1. `regression_tiers`: the overnight tier has no GT. The periodic overnight run accepts it, and it is never signed
   off.
2. `claude_sweep`: 4 unresolved flags (gi6, gi1, gi2, gi8). These are OPEN DEFECTS owned by F1 (`wip/maze-fix`).
All other gates PASS, viewer included. The mismatch gate's PASS covers only its default seeds (7001/8001 x 60). The
extended 5a run above found one fd-diverge (s121065 gi64), an open defect proposed for F1.

**P1c's d3 b0 game** (PID 73606, seed 91005 gi4, `build/scratch/maze/p1c/b0_gi4/`) was still running at 6 h 42 min
when V1 finished. There is no result yet. It matches the S4 finding that budget-0 play is degenerate on this deck.

**Open items and USER sign-off pending (V1; added to the S4 list, nothing settled):**
* The play budget b20 starves this deck: b200 is 0.225 turns/game faster on 40 seeds, and 7 of 13 slow games
  recover at b100-b500. The depth does not bind at b20. Raising the budget is a USER settings decision.
* P2 (`MTG_MAZE_SPEND_NEVERCAST`) is byte-inert on top of P1: 100/100 identical digests. Keep it (it matters only
  with P1 off) or delete it? USER-directed, so it is left as is.
* 5i: a bucket discard policy is still to be authored and reported, even under STATUS_QUO_OK (the skill's rule).
  The stakes are tiny.
* 5g mining (about 100-150 core-hours at the skill's sizing): UN-RUN; scheduling it is the USER's call.
* The full `MTG_UNPRUNED` oracle diff is memory-blocked (6 of 100 games). It needs a quiet box at 1-2 workers.
* New open defects, proposed for F1: the fd-diverge s121065 gi64 (an executor payment taps Maze's End before its
  breakpoint-replayed activation), and the gi2-class missed extra drop, seen again in gi49 and gi122 (gi122 is not
  recovered at 100x budget).

<!-- verify_deck:begin (generated -- do not edit inside) -->
## Last verification (2026-11-18)

`verify_deck.py decks/Maze's End/Maze's End.cod --write-ledger` -> **FAIL**

| Gate | Status | Blocking | Summary |
|---|---|---|---|
| coverage | PASS | yes | all 33 cards full (missing=0, partial=0) |
| card_costs | PASS | yes | all mana costs match Scryfall (cost/cmc only) |
| card_fields | PASS | yes | 853 cards match snapshot (cost/PT/types/keywords); 18 allowlisted divergence(s) |
| clause_ledger | SKIP | no | covered by coverage+bracket-notes+oracle-diff |
| regression_tiers | FAIL | yes | PARTIAL suite addition (overnight: no GT) |
| viewer | PASS | yes | self-guard + surface sweep clean |
| viewer_wiring | PASS | yes | 5 type(s) wired (emitter + GUI): choose_color, dig, land_entry, target, x_value |
| mismatch | PASS | yes | no nonconv/fd-diverge across seeds [7001, 8001] x 60 games (both arms completed) |
| play_invariants | PASS | yes | 8 game(s)/458 decisions: determinism+integrity+progress hold |
| claude_sweep | FAIL | yes | 4 unresolved play flag(s) |
| suite | PASS | yes | suite gate: Maze's End is regression key `mazesend` |

### Pending user sign-off (block the gate until fixed OR approved below)
Add a key to `## Approved deferrals` to sign one off (only if it is a genuine, understood deferral -- not a bug):
- `regression_tiers:partial` -- overnight: no GT -- all tiers or none; never sign this off. Add the missing rows and accept each tier's GT (`bash test/regression.sh --<tier> --deck=<key>`, inspect, `--<tier> --accept --deck=<key>`)
- `claude_sweep:unresolved` -- 4 unresolved flag(s) in the recorded sweep

### Stage 6a disclosure (deferrals + not-yet-built checks)
- coverage deferral -- Maze's End: NOT a Gate itself. The activation is the TapTutorPut route (Seahunter's), its library filter read off THIS card's params (tutor_required_subtype Gate + tutor_types Land, Planar Nexus included through its every-nonbasic-type bit). {T} is paid before the {3} (it cannot pay for itself); the RETURN-TO-HAND is part of the cost (tap_tutor_put_return_self, paid first at resolution -- nothing can respond in a goldfish). The Gate enters through EnterLand: its own enters-tapped, landfall and Guild Summit's draw. The WIN CHECK runs after the put AND on a fail-to-find (GameState::won_by_effect, CR 104.2b), counting distinct NAMES among Gates we control. The target is SEARCHED: one action per distinct Gate name plus fail-to-find; the deck provider collapses the menu to one target when the activation wins regardless of target (a sound fold: every such target reaches the same terminal state). The bounce is new hand material, so the hand-entry breakpoint re-solves and a replay (land drop) plus a second activation is reachable the same turn.
- coverage deferral -- Azorius Guildgate: Gate (CR 205.3i): counted BY NAME for Maze's End's "ten or more Gates with different names" -- the land-drop signature, LandSearchClasses and behaviour_identity keep its name, so it is never folded with another Gate. Colour choice on tap = the mana-payment exemption.
- coverage deferral -- Rakdos Guildgate: Gate (CR 205.3i): counted BY NAME for Maze's End's "ten or more Gates with different names" -- the land-drop signature, LandSearchClasses and behaviour_identity keep its name, so it is never folded with another Gate. Colour choice on tap = the mana-payment exemption.
- coverage deferral -- Selesnya Guildgate: Gate (CR 205.3i): counted BY NAME for Maze's End's "ten or more Gates with different names" -- the land-drop signature, LandSearchClasses and behaviour_identity keep its name, so it is never folded with another Gate. Colour choice on tap = the mana-payment exemption.
- coverage deferral -- Exploration: STATIC: one extra land drop per copy, copies stack (CR 305.2), folded into the player's bonus land drops (the untap step seeds the controlled-copy total; a copy entering adds its drop that turn). Every extra drop is SEARCHED: a plan carries one land drop, so a section that plays a land while another drop is open and a land is in hand opens the site-10 breakpoint, whose continuation enumerates the next drop (and its own fetch / as-enters choices) -- every sequence of drops is findable at an unlimited budget. Beyond the horizon the greedy drop takes every open drop (the permitted playout policy). Losing a copy mid-turn does not remove its drop this turn (only our own Archdruid's Charm could exile it, never chosen autonomously; disclosed).
- coverage deferral -- Burgeoning: One trigger per copy per opponent land PLAYED (a fetch's search is not a play), on the passive opponent's turn: the card turns the 50-card opponent land model on (ledger decision 6), so the trigger frequency is that model's (at most one land per opponent turn, only while it holds one -- disclosed). WHICH land (each distinct name in hand, crossed with its searched as-enters choice: a colour, Gateway Plaza's pay-or-sacrifice) or NONE is SEARCHED where the line crosses into the opponent's turn (the crossing-land pin); beyond the horizon the first land is the playout default. The land is OURS on their turn: enters-tapped, Gond Gate, Amulet of Vigor (no mana float off our main phase), Gateway Plaza and Guild Summit's draw all apply to us. Not a land drop.
- coverage deferral -- Simic Guildgate: Gate (CR 205.3i): counted BY NAME for Maze's End's "ten or more Gates with different names" -- the land-drop signature, LandSearchClasses and behaviour_identity keep its name, so it is never folded with another Gate. Colour choice on tap = the mana-payment exemption.
- coverage deferral -- Gruul Guildgate: Gate (CR 205.3i): counted BY NAME for Maze's End's "ten or more Gates with different names" -- the land-drop signature, LandSearchClasses and behaviour_identity keep its name, so it is never folded with another Gate. Colour choice on tap = the mana-payment exemption.
- coverage deferral -- Golgari Guildgate: Gate (CR 205.3i): counted BY NAME for Maze's End's "ten or more Gates with different names" -- the land-drop signature, LandSearchClasses and behaviour_identity keep its name, so it is never folded with another Gate. Colour choice on tap = the mana-payment exemption.
- coverage deferral -- Dimir Guildgate: Gate (CR 205.3i): counted BY NAME for Maze's End's "ten or more Gates with different names" -- the land-drop signature, LandSearchClasses and behaviour_identity keep its name, so it is never folded with another Gate. Colour choice on tap = the mana-payment exemption.
- coverage deferral -- Orzhov Guildgate: Gate (CR 205.3i): counted BY NAME for Maze's End's "ten or more Gates with different names" -- the land-drop signature, LandSearchClasses and behaviour_identity keep its name, so it is never folded with another Gate. Colour choice on tap = the mana-payment exemption.
- coverage deferral -- Boros Guildgate: Gate (CR 205.3i): counted BY NAME for Maze's End's "ten or more Gates with different names" -- the land-drop signature, LandSearchClasses and behaviour_identity keep its name, so it is never folded with another Gate. Colour choice on tap = the mana-payment exemption.
- coverage deferral -- Izzet Guildgate: Gate (CR 205.3i): counted BY NAME for Maze's End's "ten or more Gates with different names" -- the land-drop signature, LandSearchClasses and behaviour_identity keep its name, so it is never folded with another Gate. Colour choice on tap = the mana-payment exemption.
- coverage deferral -- Sylvan Scrying: tutor_to_hand + tutor_types [Land
- coverage deferral -- Amulet of Vigor: One trigger per Amulet per permanent that enters TAPPED under our control, at every entry route: the land drop, every effect put (EnterLand: Maze's End's fetch, Archdruid's Charm's tapped put), and a nonland entrant (FireEtbWatchers). Resolved right after the permanent enters, BEFORE its other enter triggers (untap-first is weakly dominant: Gateway Plaza can then pay its own {1}). With N >= 2 Amulets each trigger but the last lets the land tap for mana in response (a mana ability), so a tapped-entering land yields N-1 floating mana in our main phase and still ends untapped -- taken automatically (no mana burn; weakly dominant), its colour by the shared tap-first commit (mana-payment exemption). No float for a nonland, for a land whose tap has a side effect (Thran Portal's life), or outside our main phase (I3's opponent-turn / end-step puts). A replacement that makes the permanent enter untapped (Gond Gate) means no trigger. With an Amulet out the deck provider declines The Black Gate's 3 life (enters tapped, Amulet untaps it: a sound dominance).
- coverage deferral -- Manabond: One optional trigger per copy. Yes/no is SEARCHED where the line crosses the turn boundary (the crossing-land pin; rank 0 = yes with a land in hand), together with each put land's own as-enters choice. The lands enter SIMULTANEOUSLY: each one's tapped-ness is judged against the board before the event (CR 614.12: a Gond Gate put in the same batch untaps none of them). Then the rest of the hand is discarded, then the enter triggers resolve (Gateway Plaza's pay, Guild Summit's draws -- those cards are kept). Amulet of Vigor untaps but floats no mana (an end step). The reveal is unobservable to the goldfish.
- coverage deferral -- Guild Summit: ETB (etb_tap_subtype_draw Gate): the COUNT N is SEARCHED on the ETB choice axis (0 = decline .. every untapped Gate); WHICH N Gates follows the deck provider's mana-source rank, least valuable first (USER decision pending -- ledger decision 2; rests on the mana-payment exemption), clamped at resolution to the untapped Gates then present. WATCHER (draw_on_other_subtype_enter Gate): fires at OnLandEntered, the once-per-land chokepoint, so a Gate entering by land drop, by Maze's End's fetch, by a tutor put or any other route draws once. An empty-library draw is skipped (the existing 6a disclosure).
- coverage deferral -- Thran Portal: Fastland condition (fastland_max_other_lands 2), judged for its controller. The basic land type is an as-enters choice locked per object (etb_choose_color over WUBRG = Plains/Island/Swamp/Mountain/Forest), SEARCHED as the land-entry pin (one land option per admitted type; the deck provider restricts to colours the list can spend: Island/Mountain/Forest -- a sound prune, ledger decision 1). The chosen type is read by LandHasSubtypeNow (chosen_color_adds_basic_land_type). Its only mana ability is the basic type's intrinsic one (CR 305.6), so it makes the chosen colour only; each activation costs 1 LIFE as a cost (tap_self_damage + tap_is_life_payment, the horizon-land shape -- never damage). NOT a basic land (no Basic supertype). In hand: the full menu. Gate (CR 205.3i): counted BY NAME for Maze's End, never folded with another Gate.
- coverage deferral -- Sea Gate: The chosen colour is an as-enters replacement (CR 614.12) locked per object (etb_choose_color + etb_choose_color_fixed U: the menu excludes the printed colour); a re-entering Gate chooses afresh. The locked land makes {U} or the chosen colour (the colour per tap = mana-payment exemption). SEARCHED: one land option per admitted colour at the land drop, one Maze's End fetch variant per colour; the deck provider restricts the menu to colours the list can spend (G/U/R -- a sound prune, ledger decision 1). An effect put that carries no pin (Archdruid's Charm, I3's puts) takes the provider's colour (ledger I2 decision). Human play: the viewer's choose_color. Gate (CR 205.3i): counted BY NAME for Maze's End, never folded with another Gate.
- coverage deferral -- Manor Gate: The chosen colour is an as-enters replacement (CR 614.12) locked per object (etb_choose_color + etb_choose_color_fixed G: the menu excludes the printed colour); a re-entering Gate chooses afresh. The locked land makes {G} or the chosen colour (the colour per tap = mana-payment exemption). SEARCHED: one land option per admitted colour at the land drop, one Maze's End fetch variant per colour; the deck provider restricts the menu to colours the list can spend (G/U/R -- a sound prune, ledger decision 1). An effect put that carries no pin (Archdruid's Charm, I3's puts) takes the provider's colour (ledger I2 decision). Human play: the viewer's choose_color. Gate (CR 205.3i): counted BY NAME for Maze's End, never folded with another Gate.
- coverage deferral -- Heap Gate: Mana: the Capital City shape (any_color_filter): the free {C}, or {1} fed in -> one mana of any colour (the colour is the mana-payment exemption). Treasure (PermAbilityMode::TapTreasure, tap_treasure_cost {1} + tap_treasure_tap_other_subtype Gate): NOT a mana ability (it makes a token), a SEARCHED plan action like every activation, once per untap. Costs in order: the source's {T} (so the cost Gate is necessarily ANOTHER Gate, CR 601.2h), then the other Gate's tap (PayActivationOtherTap, both worlds), then the {1}. WHICH Gate the cost taps follows the deck's Gate tap order (pure-mana Gates first, Baldur's Gate next, a Gate with a live non-mana {T} ability last; ManaSourceRank inside) -- the mana-payment exemption's ordering, ledger decision 30; a human picks it on the board. The Treasure is the shared 'Treasure Token' (a payment source). By CR 106.7 it makes Gond Gate produce every colour. Gate (CR 205.3i): counted BY NAME for Maze's End, never folded with another Gate.
- coverage deferral -- Gond Gate: Static (gates_enter_untapped): a replacement judged for the ENTERING Gate's controller inside LandWouldEnterTapped, first, so it reaches every route (land drop, Maze's End's fetch, tutor puts) and no shock life is paid for a Gate under it (The Black Gate). It also untaps a Gate an effect puts onto the battlefield TAPPED (Archdruid's Charm): the controller orders the two replacements (CR 616.1) -- PROVISIONAL, ledger decision 5. A Gate entering SIMULTANEOUSLY with a Gond Gate is not untapped by it (CR 614.12: simultaneous puts judge the pre-event board). Gond Gate has no enters-tapped clause itself. Mana (mana_colors_of_controlled_subtype Gate): {C}, plus any COLOUR a Gate we control could produce (CR 106.7: costs ignored, so Planar Nexus / Gateway Plaza / Heap Gate give all five; a CLB Gate its printed + chosen colour, Thran Portal its chosen one; tapped Gates count; another Gond's reflected mode is circular and ignored). In hand: {C} only. Gate (CR 205.3i): counted BY NAME for Maze's End, never folded with another Gate.
- coverage deferral -- Cliffgate: The chosen colour is an as-enters replacement (CR 614.12) locked per object (etb_choose_color + etb_choose_color_fixed R: the menu excludes the printed colour); a re-entering Gate chooses afresh. The locked land makes {R} or the chosen colour (the colour per tap = mana-payment exemption). SEARCHED: one land option per admitted colour at the land drop, one Maze's End fetch variant per colour; the deck provider restricts the menu to colours the list can spend (G/U/R -- a sound prune, ledger decision 1). An effect put that carries no pin (Archdruid's Charm, I3's puts) takes the provider's colour (ledger I2 decision). Human play: the viewer's choose_color. Gate (CR 205.3i): counted BY NAME for Maze's End, never folded with another Gate.
- coverage deferral -- Black Dragon Gate: The chosen colour is an as-enters replacement (CR 614.12) locked per object (etb_choose_color + etb_choose_color_fixed B: the menu excludes the printed colour); a re-entering Gate chooses afresh. The locked land makes {B} or the chosen colour (the colour per tap = mana-payment exemption). SEARCHED: one land option per admitted colour at the land drop, one Maze's End fetch variant per colour; the deck provider restricts the menu to colours the list can spend (G/U/R -- a sound prune, ledger decision 1). An effect put that carries no pin (Archdruid's Charm, I3's puts) takes the provider's colour (ledger I2 decision). Human play: the viewer's choose_color. Gate (CR 205.3i): counted BY NAME for Maze's End, never folded with another Gate.
- coverage deferral -- Baldur's Gate: The Three Tree City scaled-mana-land shape: the free {C} is `produces`; the {2}, {T} mode (mana_per_creature_feeder_generic 2) adds N mana of ONE colour, N = the OTHER Gates we control at activation, tapped or not (mana_per_other_controlled_subtype Gate; ScaledManaCreatureCount). The colour is the payment's (mana-payment exemption). Like Three Tree City the payer takes the fed mode only when it nets at least one mana over the free {C} (net = N - 2 >= 1, the inherited approximation; ScaledManaNetYield). By CR 106.7 it makes Gond Gate produce every colour (ControlledSubtypeColors). LEGENDARY: one copy; the legend rule applies. Gate (CR 205.3i): counted BY NAME for Maze's End, never folded with another Gate.
- coverage deferral -- Citadel Gate: The chosen colour is an as-enters replacement (CR 614.12) locked per object (etb_choose_color + etb_choose_color_fixed W: the menu excludes the printed colour); a re-entering Gate chooses afresh. The locked land makes {W} or the chosen colour (the colour per tap = mana-payment exemption). SEARCHED: one land option per admitted colour at the land drop, one Maze's End fetch variant per colour; the deck provider restricts the menu to colours the list can spend (G/U/R -- a sound prune, ledger decision 1). An effect put that carries no pin (Archdruid's Charm, I3's puts) takes the provider's colour (ledger I2 decision). Human play: the viewer's choose_color. Gate (CR 205.3i): counted BY NAME for Maze's End, never folded with another Gate.
- coverage deferral -- Basilisk Gate: The pump is the TapTargetPump {T} mode (Kessig Wolf Run): +1/+1 times the Gates we control at RESOLUTION, the source included (tap_target_pump_per_subtype_both Gate). Sorcery speed: TapTargetPump is enumerated only in our main phases' plans. The target is searched among OUR creatures autonomously (one per identity class); an opponent's creature is legal but never offered autonomously (the passive goldfish opponent never attacks or blocks), and a human is offered every legal target. INERT in this list: no creature of ours, none made, so the ability is never offered autonomously (implemented anyway, ledger decision 3). Gate (CR 205.3i): counted BY NAME for Maze's End, never folded with another Gate.
- coverage deferral -- Gates Ablaze: X counted at resolution over EVERY Gate we control (duplicates included; Planar Nexus counts). SYMMETRIC (our creatures too). OUTCOME-INERT in a goldfish: this list has no creatures and the passive opponent's creatures never attack or block, and nothing watches their deaths (the Pyroclasm disclosure). The deck provider never offers the cast autonomously (deck-scoped dominance); a human may cast it.
- coverage deferral -- Gateway Plaza: Enters tapped. The ETB pay-or-sacrifice (etb_sacrifice_unless_pay {1}) resolves at every land-entry route, AFTER Amulet of Vigor's untap (sound dominance order), so with an Amulet or Gond Gate it can pay with itself. Pay vs sacrifice is SEARCHED (the land-entry pin: one land option / one Maze's End fetch variant each) when the {1} is affordable; unaffordable = forced sacrifice. Human play: the `dig` chooser (pay = keep the card; the Mihail pay-{1} shape -- ledger decision 4). Any colour on tap (City of Brass shape, painless). Gate (CR 205.3i): counted BY NAME for Maze's End, never folded with another Gate.
- coverage deferral -- Archdruid's Charm: MODE 1 = the Crop Rotation route (tutor_land_to_battlefield, no additional cost): the find is the SEARCHED tutor axis; a land enters TAPPED by the effect (LandEntryTap::ForcedTapped -- its own enters-tapped rule is not consulted, Amulet sees a tapped entry), a creature goes to HAND (tutor_nonland_to_hand); then shuffle. Reveal is unobservable. MODE 2 needs a creature we control as a target and this list has none and makes none, so it can never be chosen (CR 601.2c) -- not modelled (PROVISIONAL, ledger). MODE 3 (modal_exile_target_artifact_enchantment): the passive opponent controls no artifact or enchantment, so its only legal targets are our own; dominated by not casting, so the deck provider never offers it autonomously; human play offers one cast variant per legal target (the AX<n> pin, exiled at resolution if still legal).
- coverage deferral -- Planar Nexus: Every nonbasic land type in EVERY zone (a characteristic-defining ability, CR 604.3): all_nonbasic_land_types sets a bit on the card's subtype set, read as a fallback by every subtype reader (CardHasSubtype, LandHasSubtypeNow, SubtypeSet::HasId) against CR 205.3i's nonbasic list. So it IS a Gate for Maze's End's search and distinct-name count, Guild Summit's draw and tap, and Gates Ablaze. NO basic land type and no Basic supertype. Mana: Capital City's any_color_filter ({T}: {C} implicit; {1},{T}: one mana of any colour). The other nonbasic types have no reader in this list (inert).
- coverage deferral -- Talon Gates of Madara: Mana: the Capital City shape (any_color_filter). {4} put (hand_put_self_cost): a from-HAND activated ability riding the Channel action (one per distinct name; viewer verb channel=, badged 'put'): pay {4}, the card leaves the hand and enters through EnterLand -- NOT a land play (the land drop is untouched), untapped, so Guild Summit's draw, Amulet / Gond, landfall all fire, and it can tap for mana at once. Offered in OUR main phases only: an instant-speed put on the goldfish opponent's turn leaves the same board at our next untap and commutes with a Burgeoning put of another land, so main-phase timing is a sound fold (ledger decision 32). In its plan's trailing pass it runs FIRST, before Maze's End's activation (sound order: it only adds a Gate and mana; ledger decision 31). ETB 'up to one target creature phases out': NOT modelled (the engine has no phasing) -- inert in a goldfish: the list has no creature of ours, and the passive opponent's creatures never attack or block, so every choice (none included) reaches the same game; disclosed as an inert collapse, never offered as a choice. By CR 106.7 it makes Gond Gate produce every colour. Gate (CR 205.3i): counted BY NAME for Maze's End, never folded with another Gate.
- coverage deferral -- The Black Gate: Shock-style entry (etb_pay_life_to_untap 3), judged for its controller: autonomous play pays when life > 3 and the mana is wanted (the shock rule; goldfish life is nearly free -- the life-payment disclosure), declines under an Amulet of Vigor (sound dominance), and never pays under a Gond Gate (it enters untapped anyway). Human play: the land_entry chooser. The {1}{B},{T} evasion ability (TapTargetPump, grants_unblockable) needs a target creature and this list has none and makes none, so it is never offered (inert); the 'player with the most life' choice is collapsed to the opponent (when we have more life the real ability does nothing). LEGENDARY: one copy; the legend rule applies. Gate (CR 205.3i): counted BY NAME for Maze's End, never folded with another Gate.
- allowlisted divergence -- Wanderwine Prophets [keywords]: Champion is modeled by champion_merfolk with linked exile, return, sacrifice if no champion, and searched human choice. The engine has no generic cham
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
- allowlisted divergence -- Bloodline Keeper [keywords]: Transforming DFC (Bloodline Keeper // Lord of Lineage): Scryfall reports CARD-LEVEL keywords for both faces (Flying, Transform). Transform is the layo
- allowlisted divergence -- Lord of Lineage [keywords]: Transforming DFC back face: Scryfall's card-level keywords include the layout's Transform; the back face carries no transform of its own (transform_fr
- allowlisted divergence -- Emrakul, the Aeons Torn [keywords]: 'Protection from spells that are one or more colors' is a real keyword but INERT here (PROVISIONAL, ledger Q12): nothing in a goldfish game targets, d
- oracle_text advisory -- Deeproot Pilgrimage: oracle_text diverges (similarity 0.55); scryfall='Whenever one or more nontoken Merfolk you control become tapped, create a 1/1 blue Merfolk creature token with
- oracle_text advisory -- Svyelun of Sea and Sky: oracle_text diverges (similarity 0.73); scryfall='Svyelun has indestructible as long as you control at least two other Merfolk.\nWhenever Svyelun attacks, draw 
- oracle_text advisory -- Wanderwine Prophets: oracle_text diverges (similarity 0.11); scryfall='Champion a Merfolk (When this enters, sacrifice it unless you exile another Merfolk you control. When this lea
- oracle_text advisory -- Decimator of the Provinces: oracle_text diverges (similarity 0.22); scryfall="Emerge {6}{G}{G}{G} (You may cast this spell by sacrificing a creature and paying the emerge cost reduced by t
- oracle_text advisory -- End-Raze Forerunners: oracle_text diverges (similarity 0.15); scryfall='Vigilance, trample, haste\nWhen this creature enters, other creatures you control get +2/+2 and gain vigilance
- oracle_text advisory -- Flinthoof Boar: oracle_text diverges (similarity 0.17); scryfall='This creature gets +1/+1 as long as you control a Mountain.\n{R}: This creature gains haste until end of turn.
- oracle_text advisory -- Herald of Ilharg: oracle_text diverges (similarity 0.24); scryfall='Trample\nWhenever you cast a creature spell, put two +1/+1 counters on this creature. If that spell has mana v
- oracle_text advisory -- Herald's Horn: oracle_text diverges (similarity 0.26); scryfall="As this artifact enters, choose a creature type.\nCreature spells you cast of the chosen type cost {1} less to
- oracle_text advisory -- Ilharg, the Raze-Boar: oracle_text diverges (similarity 0.21); scryfall="Trample\nWhenever Ilharg attacks, you may put a creature card from your hand onto the battlefield tapped and a
- oracle_text advisory -- Raggadragga, Goreguts Boss: oracle_text diverges (similarity 0.23); scryfall='Each creature you control with a mana ability gets +2/+2.\nWhenever a creature you control with a mana ability
- oracle_text advisory -- Gideon, Ally of Zendikar: oracle_text diverges (similarity 0.10); scryfall='+1: Until end of turn, Gideon becomes a 5/5 Human Soldier Ally creature with indestructible that\'s still a pl
- oracle_text advisory -- Basri, Tomorrow's Champion: oracle_text diverges (similarity 0.15); scryfall="{W}, {T}, Exert Basri: Create a 1/1 white Cat creature token with lifelink. (An exerted creature won't untap d
- oracle_text advisory -- Goblin Guide: oracle_text diverges (similarity 0.60); scryfall="Haste\nWhenever this creature attacks, defending player reveals the top card of their library. If it's a land 
- oracle_text advisory -- Light Up the Stage: oracle_text diverges (similarity 0.69); scryfall='Spectacle {R} (You may cast this spell for its spectacle cost rather than its mana cost if an opponent lost li
- oracle_text advisory -- Crystalline Sliver: oracle_text diverges (similarity 0.61); scryfall="All Slivers have shroud. (They can't be the targets of spells or abilities.)"
- oracle_text advisory -- Galerider Sliver: oracle_text diverges (similarity 0.41); scryfall='Sliver creatures you control have flying.'
- oracle_text advisory -- Striking Sliver: oracle_text diverges (similarity 0.31); scryfall='Sliver creatures you control have first strike. (They deal combat damage before creatures without first strike
- oracle_text advisory -- Cloudshredder Sliver: oracle_text diverges (similarity 0.48); scryfall='Sliver creatures you control have flying and haste.'
- oracle_text advisory -- Hibernation Sliver: oracle_text diverges (similarity 0.49); scryfall='All Slivers have "Pay 2 life: Return this permanent to its owner\'s hand."'
- oracle_text advisory -- Cavern of Souls: oracle_text diverges (similarity 0.40); scryfall="As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Unclaimed Territory: oracle_text diverges (similarity 0.15); scryfall='As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Secluded Courtyard: oracle_text diverges (similarity 0.21); scryfall='As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Mutavault: oracle_text diverges (similarity 0.56); scryfall="{T}: Add {C}.\n{1}: This land becomes a 2/2 creature with all creature types until end of turn. It's still a l
- oracle_text advisory -- Aether Vial: oracle_text diverges (similarity 0.35); scryfall='At the beginning of your upkeep, you may put a charge counter on this artifact.\n{T}: You may put a creature c
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
- oracle_text advisory -- Knight Exemplar: oracle_text diverges (similarity 0.27); scryfall='First strike (This creature deals combat damage before creatures without first strike.)\nOther Knight creature
- oracle_text advisory -- Valiant Knight: oracle_text diverges (similarity 0.18); scryfall='Other Knights you control get +1/+1.\n{3}{W}{W}: Knights you control gain double strike until end of turn.'
- oracle_text advisory -- Kinsbaile Cavalier: oracle_text diverges (similarity 0.05); scryfall='Knight creatures you control have double strike.'
- oracle_text advisory -- Marshal of Zhalfir: oracle_text diverges (similarity 0.49); scryfall='Other Knights you control get +1/+1.\n{W}{U}, {T}: Tap another target creature.'
- oracle_text advisory -- Haytham Kenway: oracle_text diverges (similarity 0.53); scryfall='Protection from Assassins\nOther Knights you control get +2/+2 and have protection from Assassins.\nWhen Hayth
- oracle_text advisory -- Adeline, Resplendent Cathar: oracle_text diverges (similarity 0.76); scryfall="Vigilance\nAdeline's power is equal to the number of creatures you control.\nWhenever you attack, for each opp
- oracle_text advisory -- Accorder Paladin: oracle_text diverges (similarity 0.33); scryfall='Battle cry (Whenever this creature attacks, each other attacking creature gets +1/+0 until end of turn.)'
- oracle_text advisory -- Silverblade Paladin: oracle_text diverges (similarity 0.25); scryfall='Soulbond (You may pair this creature with another unpaired creature when either enters. They remain paired for
- oracle_text advisory -- Niv-Mizzet, the Firemind: oracle_text diverges (similarity 0.16); scryfall='Flying\nWhenever you draw a card, Niv-Mizzet deals 1 damage to any target.\n{T}: Draw a card.'
- oracle_text advisory -- Niv-Mizzet, Parun: oracle_text diverges (similarity 0.23); scryfall="This spell can't be countered.\nFlying\nWhenever you draw a card, Niv-Mizzet deals 1 damage to any target.\nWh
- oracle_text advisory -- Curiosity: oracle_text diverges (similarity 0.16); scryfall='Enchant creature\nWhenever enchanted creature deals damage to an opponent, you may draw a card.'
- oracle_text advisory -- Tandem Lookout: oracle_text diverges (similarity 0.38); scryfall='Soulbond (You may pair this creature with another unpaired creature when either enters. They remain paired for
- oracle_text advisory -- Hero of Bladehold: oracle_text diverges (similarity 0.50); scryfall='Battle cry (Whenever this creature attacks, each other attacking creature gets +1/+0 until end of turn.)\nWhen
- oracle_text advisory -- Windswept Heath: oracle_text diverges (similarity 0.30); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Plains card, put it onto the battlef
- oracle_text advisory -- Marsh Flats: oracle_text diverges (similarity 0.32); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Plains or Swamp card, put it onto the battlefi
- oracle_text advisory -- Bloodstained Mire: oracle_text diverges (similarity 0.32); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Mountain card, put it onto the battle
- oracle_text advisory -- Wooded Foothills: oracle_text diverges (similarity 0.45); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Mountain or Forest card, put it onto the battl
- oracle_text advisory -- Grove of the Burnwillows: oracle_text diverges (similarity 0.20); scryfall='{T}: Add {C}.\n{T}: Add {R} or {G}. Each opponent gains 1 life.'
- oracle_text advisory -- Ignoble Hierarch: oracle_text diverges (similarity 0.56); scryfall='Exalted (Whenever a creature you control attacks alone, that creature gets +1/+1 until end of turn.)\n{T}: Add
- oracle_text advisory -- Skyshroud Cutter: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 5 life."
- oracle_text advisory -- Plague Drone: oracle_text diverges (similarity 0.70); scryfall='Flying\nRot Fly — If an opponent would gain life, that player loses that much life instead.'
- oracle_text advisory -- Aria of Flame: oracle_text diverges (similarity 0.78); scryfall='When this enchantment enters, each opponent gains 10 life.\nWhenever you cast an instant or sorcery spell, put
- oracle_text advisory -- Fiery Justice: oracle_text diverges (similarity 0.54); scryfall='Fiery Justice deals 5 damage divided as you choose among any number of targets. Target opponent gains 5 life.'
- oracle_text advisory -- Swords to Plowshares: oracle_text diverges (similarity 0.17); scryfall='Exile target creature. Its controller gains life equal to its power.'
- oracle_text advisory -- Invigorate: oracle_text diverges (similarity 0.52); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have an opponent gain 3 life.\nTarget
- oracle_text advisory -- Reverent Silence: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 6 life.\n
- oracle_text advisory -- Idyllic Tutor: oracle_text diverges (similarity 0.30); scryfall='Search your library for an enchantment card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Enlightened Tutor: oracle_text diverges (similarity 0.32); scryfall='Search your library for an artifact or enchantment card, reveal it, then shuffle and put that card on top.'
- oracle_text advisory -- Forbidden Orchard: oracle_text diverges (similarity 0.22); scryfall='{T}: Add one mana of any color.\nWhenever you tap this land for mana, target opponent creates a 1/1 colorless 
- oracle_text advisory -- Reflecting Pool: oracle_text diverges (similarity 0.26); scryfall='{T}: Add one mana of any type that a land you control could produce.'
- oracle_text advisory -- Izzet Signet: oracle_text diverges (similarity 0.05); scryfall='{1}, {T}: Add {U}{R}.'
- oracle_text advisory -- Ponder: oracle_text diverges (similarity 0.14); scryfall='Look at the top three cards of your library, then put them back in any order. You may shuffle.\nDraw a card.'
- oracle_text advisory -- Preordain: oracle_text diverges (similarity 0.27); scryfall='Scry 2, then draw a card. (To scry 2, look at the top two cards of your library, then put any number of them o
- oracle_text advisory -- Expressive Iteration: oracle_text diverges (similarity 0.45); scryfall='Look at the top three cards of your library. Put one of them into your hand, put one of them on the bottom of 
- oracle_text advisory -- Crackle with Power: oracle_text diverges (similarity 0.17); scryfall='Crackle with Power deals five times X damage to each of up to X targets.'
- oracle_text advisory -- Remand: oracle_text diverges (similarity 0.12); scryfall="Counter target spell. If that spell is countered this way, put it into its owner's hand instead of into that p
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
- oracle_text advisory -- Lotus Bloom: oracle_text diverges (similarity 0.23); scryfall='Suspend 3—{0} (Rather than cast this card from your hand, pay {0} and exile it with three time counters on it.
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
- oracle_text advisory -- Ethereal Armor: oracle_text diverges (similarity 0.33); scryfall='Enchant creature\nEnchanted creature gets +1/+1 for each enchantment you control and has first strike.'
- oracle_text advisory -- Rancor: oracle_text diverges (similarity 0.68); scryfall="Enchant creature\nEnchanted creature gets +2/+0 and has trample.\nWhen this Aura is put into a graveyard from 
- oracle_text advisory -- Daybreak Coronet: oracle_text diverges (similarity 0.35); scryfall='Enchant creature with another Aura attached to it\nEnchanted creature gets +3/+3 and has first strike, vigilan
- oracle_text advisory -- Armadillo Cloak: oracle_text diverges (similarity 0.77); scryfall='Enchant creature\nEnchanted creature gets +2/+2 and has trample.\nWhenever enchanted creature deals damage, yo
- oracle_text advisory -- Hyena Umbra: oracle_text diverges (similarity 0.52); scryfall='Enchant creature\nEnchanted creature gets +1/+1 and has first strike.\nUmbra armor (If enchanted creature woul
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
- oracle_text advisory -- Muxus, Goblin Grandee: oracle_text diverges (similarity 0.46); scryfall='When Muxus enters, reveal the top six cards of your library. Put all Goblin creature cards with mana value 5 o
- oracle_text advisory -- Goblin Chainwhirler: oracle_text diverges (similarity 0.24); scryfall='First strike\nWhen this creature enters, it deals 1 damage to each opponent and each creature and planeswalker
- oracle_text advisory -- Twinshot Sniper: oracle_text diverges (similarity 0.50); scryfall='Reach\nWhen this creature enters, it deals 2 damage to any target.\nChannel — {1}{R}, Discard this card: It de
- oracle_text advisory -- Stingscourger: oracle_text diverges (similarity 0.71); scryfall="Echo {3}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
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
- oracle_text advisory -- Misty Rainforest: oracle_text diverges (similarity 0.29); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Island card, put it onto the battlef
- oracle_text advisory -- Verdant Catacombs: oracle_text diverges (similarity 0.26); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Forest card, put it onto the battlefi
- oracle_text advisory -- Scalding Tarn: oracle_text diverges (similarity 0.16); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for an Island or Mountain card, put it onto the batt
- oracle_text advisory -- Cosmic Spider-Man: oracle_text diverges (similarity 0.33); scryfall='Flying, first strike, trample, lifelink, haste\nAt the beginning of combat on your turn, other Spiders you con
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
- oracle_text advisory -- Braingeyser: oracle_text diverges (similarity 0.08); scryfall='Target player draws X cards.'
- oracle_text advisory -- Terror: oracle_text diverges (similarity 0.32); scryfall="Destroy target nonartifact, nonblack creature. It can't be regenerated."
- oracle_text advisory -- Shivan Dragon: oracle_text diverges (similarity 0.32); scryfall='Flying\n{R}: This creature gets +1/+0 until end of turn.'
- oracle_text advisory -- Regrowth: oracle_text diverges (similarity 0.46); scryfall='Return target card from your graveyard to your hand.'
- oracle_text advisory -- Unite the Coalition: oracle_text diverges (similarity 0.35); scryfall="Choose five. You may choose the same mode more than once.\n• Target permanent phases out.\n• Target player dra
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
- oracle_text advisory -- Paradise Mantle: oracle_text diverges (similarity 0.10); scryfall='Equipped creature has "{T}: Add one mana of any color."\nEquip {1}'
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
- oracle_text advisory -- Kor Duelist: oracle_text diverges (similarity 0.43); scryfall='As long as this creature is equipped, it has double strike. (It deals both first-strike and regular combat dam
- oracle_text advisory -- Puresteel Paladin: oracle_text diverges (similarity 0.34); scryfall='Whenever an Equipment you control enters, you may draw a card.\nMetalcraft — Equipment you control have equip 
- oracle_text advisory -- Balan, Wandering Knight: oracle_text diverges (similarity 0.30); scryfall='First strike\nBalan has double strike as long as two or more Equipment are attached to it.\n{1}{W}: Attach all
- oracle_text advisory -- Armored Skyhunter: oracle_text diverges (similarity 0.49); scryfall='Flying\nWhenever this creature attacks, look at the top six cards of your library. You may put an Aura or Equi
- oracle_text advisory -- Kemba, Kha Regent: oracle_text diverges (similarity 0.34); scryfall='At the beginning of your upkeep, create a 2/2 white Cat creature token for each Equipment attached to Kemba.'
- oracle_text advisory -- Stoneforge Mystic: oracle_text diverges (similarity 0.44); scryfall='When this creature enters, you may search your library for an Equipment card, reveal it, put it into your hand
- oracle_text advisory -- Unexpectedly Absent: oracle_text diverges (similarity 0.28); scryfall="Put target nonland permanent into its owner's library just beneath the top X cards of that library."
- oracle_text advisory -- Boros Garrison: oracle_text diverges (similarity 0.34); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {R}{
- oracle_text advisory -- Elvish Archdruid: oracle_text diverges (similarity 0.38); scryfall='Other Elf creatures you control get +1/+1.\n{T}: Add {G} for each Elf you control.'
- oracle_text advisory -- Priest of Titania: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {G} for each Elf on the battlefield.'
- oracle_text advisory -- Arbor Elf: oracle_text diverges (similarity 0.03); scryfall='{T}: Untap target Forest.'
- oracle_text advisory -- Wirewood Lodge: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.\n{G}, {T}: Untap target Elf.'
- oracle_text advisory -- Worldly Tutor: oracle_text diverges (similarity 0.41); scryfall='Search your library for a creature card, reveal it, then shuffle and put the card on top.'
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
- oracle_text advisory -- Boros Reckoner: oracle_text diverges (similarity 0.17); scryfall='Whenever this creature is dealt damage, it deals that much damage to any target.\n{R/W}: This creature gains f
- oracle_text advisory -- Burning-Fist Minotaur: oracle_text diverges (similarity 0.12); scryfall='First strike\n{1}{R}, Discard a card: This creature gets +2/+0 until end of turn.'
- oracle_text advisory -- Deathbellow Raider: oracle_text diverges (similarity 0.16); scryfall='This creature attacks each combat if able.\n{2}{B}: Regenerate this creature.'
- oracle_text advisory -- Fanatic of Mogis: oracle_text diverges (similarity 0.39); scryfall='When this creature enters, it deals damage to each opponent equal to your devotion to red. (Each {R} in the ma
- oracle_text advisory -- Gnarled Scarhide: oracle_text diverges (similarity 0.34); scryfall="Bestow {3}{B} (If you cast this card for its bestow cost, it's an Aura spell with enchant creature. It becomes
- oracle_text advisory -- Kragma Warcaller: oracle_text diverges (similarity 0.33); scryfall='Minotaur creatures you control have haste.\nWhenever a Minotaur you control attacks, it gets +2/+0 until end o
- oracle_text advisory -- Neheb, the Worthy: oracle_text diverges (similarity 0.25); scryfall='First strike\nOther Minotaurs you control have first strike.\nAs long as you have one or fewer cards in hand, 
- oracle_text advisory -- Rageblood Shaman: oracle_text diverges (similarity 0.35); scryfall='Trample\nOther Minotaur creatures you control get +1/+1 and have trample.'
- oracle_text advisory -- Ragemonger: oracle_text diverges (similarity 0.45); scryfall='Minotaur spells you cast cost {B}{R} less to cast. This effect reduces only the amount of colored mana you pay
- oracle_text advisory -- Rakdos Carnarium: oracle_text diverges (similarity 0.36); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {B}{
- oracle_text advisory -- Sethron, Hurloon General: oracle_text diverges (similarity 0.34); scryfall='Whenever Sethron or another nontoken Minotaur you control enters, create a 2/3 red Minotaur creature token.\n{
- oracle_text advisory -- Slaughter-Priest of Mogis: oracle_text diverges (similarity 0.21); scryfall='Whenever you sacrifice a permanent, this creature gets +2/+0 until end of turn.\n{2}, Sacrifice another creatu
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
- oracle_text advisory -- Stroke of Genius: oracle_text diverges (similarity 0.04); scryfall='Target player draws X cards.'
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
- oracle_text advisory -- Auriok Champion: oracle_text diverges (similarity 0.11); scryfall='Protection from black and from red\nWhenever another creature enters, you may gain 1 life.'
- oracle_text advisory -- Ocelot Pride: oracle_text diverges (similarity 0.12); scryfall="First strike, lifelink\nAscend (If you control ten or more permanents, you get the city's blessing for the res
- oracle_text advisory -- Serra Ascendant: oracle_text diverges (similarity 0.21); scryfall='Lifelink (Damage dealt by this creature also causes you to gain that much life.)\nAs long as you have 30 or mo
- oracle_text advisory -- Ajani's Pridemate: oracle_text diverges (similarity 0.09); scryfall='Whenever you gain life, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Voice of the Blessed: oracle_text diverges (similarity 0.26); scryfall='Whenever you gain life, put a +1/+1 counter on this creature.\nAs long as this creature has four or more +1/+1
- oracle_text advisory -- Daxos, Blessed by the Sun: oracle_text diverges (similarity 0.18); scryfall="Daxos's toughness is equal to your devotion to white. (Each {W} in the mana costs of permanents you control co
- oracle_text advisory -- Archangel of Thune: oracle_text diverges (similarity 0.24); scryfall='Flying\nLifelink (Damage dealt by this creature also causes you to gain that much life.)\nWhenever you gain li
- oracle_text advisory -- Heliod, Sun-Crowned: oracle_text diverges (similarity 0.13); scryfall="Indestructible\nAs long as your devotion to white is less than five, Heliod isn't a creature.\nWhenever you ga
- oracle_text advisory -- Ranger-Captain of Eos: oracle_text diverges (similarity 0.12); scryfall="When this creature enters, you may search your library for a creature card with mana value 1 or less, reveal i
- oracle_text advisory -- Ajani, Strength of the Pride: oracle_text diverges (similarity 0.27); scryfall='+1: You gain life equal to the number of creatures you control plus the number of planeswalkers you control.\n
- oracle_text advisory -- Apex Altisaur: oracle_text diverges (similarity 0.17); scryfall="When this creature enters, it fights up to one target creature you don't control.\nEnrage — Whenever this crea
- oracle_text advisory -- World War Hulk: oracle_text diverges (similarity 0.17); scryfall='(As this Saga enters and after your draw step, add a lore counter. Sacrifice after III.)\nI — The next red or 
- oracle_text advisory -- Ghalta, Stampede Tyrant: oracle_text diverges (similarity 0.17); scryfall='Trample\nWhen Ghalta enters, put any number of creature cards from your hand onto the battlefield.'
- oracle_text advisory -- Lyra Dawnbringer: oracle_text diverges (similarity 0.11); scryfall='Flying\nFirst strike (This creature deals combat damage before creatures without first strike.)\nLifelink (Dam
- oracle_text advisory -- Youthful Valkyrie: oracle_text diverges (similarity 0.08); scryfall='Flying\nWhenever another Angel you control enters, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Bishop of Wings: oracle_text diverges (similarity 0.10); scryfall='Whenever an Angel you control enters, you gain 4 life.\nWhenever an Angel you control dies, create a 1/1 white
- oracle_text advisory -- Righteous Valkyrie: oracle_text diverges (similarity 0.15); scryfall="Flying\nWhenever another Angel or Cleric you control enters, you gain life equal to that creature's toughness.
- oracle_text advisory -- Seraph Sanctuary: oracle_text diverges (similarity 0.12); scryfall='When this land enters, you gain 1 life.\nWhenever an Angel you control enters, you gain 1 life.\n{T}: Add {C}.
- oracle_text advisory -- Resplendent Angel: oracle_text diverges (similarity 0.11); scryfall='Flying\nAt the beginning of each end step, if you gained 5 or more life this turn, create a 4/4 white Angel cr
- oracle_text advisory -- Legion Angel: oracle_text diverges (similarity 0.07); scryfall='Flying\nWhen this creature enters, you may reveal a card you own named Legion Angel from outside the game and 
- oracle_text advisory -- Serra the Benevolent: oracle_text diverges (similarity 0.16); scryfall='+2: Creatures you control with flying get +1/+1 until end of turn.\n−3: Create a 4/4 white Angel creature toke
- oracle_text advisory -- Giada, Font of Hope: oracle_text diverges (similarity 0.11); scryfall='Flying, vigilance\nEach other Angel you control enters with an additional +1/+1 counter on it for each Angel y
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
- oracle_text advisory -- Saproling Migration: oracle_text diverges (similarity 0.13); scryfall='Kicker {4} (You may pay an additional {4} as you cast this spell.)\nCreate two 1/1 green Saproling creature to
- oracle_text advisory -- Doubling Season: oracle_text diverges (similarity 0.28); scryfall='If an effect would create one or more tokens under your control, it creates twice that many of those tokens in
- oracle_text advisory -- Parallel Lives: oracle_text diverges (similarity 0.13); scryfall='If an effect would create one or more tokens under your control, it creates twice that many of those tokens in
- oracle_text advisory -- Primal Vigor: oracle_text diverges (similarity 0.15); scryfall='If one or more tokens would be created, twice that many of those tokens are created instead.\nIf one or more +
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
- oracle_text advisory -- Risen Reef: oracle_text diverges (similarity 0.15); scryfall="Whenever this creature or another Elemental you control enters, look at the top card of your library. If it's 
- oracle_text advisory -- Flamekin Harbinger: oracle_text diverges (similarity 0.15); scryfall='When this creature enters, you may search your library for an Elemental card, reveal it, then shuffle and put 
- oracle_text advisory -- Foundation Breaker: oracle_text diverges (similarity 0.17); scryfall="When this creature enters, you may destroy target artifact or enchantment.\nEvoke {1}{G} (You may cast this sp
- oracle_text advisory -- Fulminator Mage: oracle_text diverges (similarity 0.07); scryfall='Sacrifice this creature: Destroy target nonbasic land.'
- oracle_text advisory -- Pyroclasm: oracle_text diverges (similarity 0.05); scryfall='Pyroclasm deals 2 damage to each creature.'
- oracle_text advisory -- Hamletback Goliath: oracle_text diverges (similarity 0.09); scryfall="Whenever another creature enters, you may put X +1/+1 counters on this creature, where X is that creature's po
- oracle_text advisory -- Borderland Behemoth: oracle_text diverges (similarity 0.06); scryfall='Trample\nThis creature gets +4/+4 for each other Giant you control.'
- oracle_text advisory -- Inferno Titan: oracle_text diverges (similarity 0.13); scryfall='{R}: This creature gets +1/+0 until end of turn.\nWhenever this creature enters or attacks, it deals 3 damage 
- oracle_text advisory -- Surtland Flinger: oracle_text diverges (similarity 0.18); scryfall="Whenever this creature attacks, you may sacrifice another creature. When you do, this creature deals damage eq
- oracle_text advisory -- Tectonic Giant: oracle_text diverges (similarity 0.16); scryfall='Whenever this creature attacks or becomes the target of a spell an opponent controls, choose one —\n• This cre
- oracle_text advisory -- Metallic Mimic: oracle_text diverges (similarity 0.17); scryfall='As this creature enters, choose a creature type.\nThis creature is the chosen type in addition to its other ty
- oracle_text advisory -- Smokebraider: oracle_text diverges (similarity 0.12); scryfall='{T}: Add two mana in any combination of colors. Spend this mana only to cast Elemental spells or activate abil
- oracle_text advisory -- Incandescent Soulstoke: oracle_text diverges (similarity 0.10); scryfall='Other Elemental creatures you control get +1/+1.\n{1}{R}, {T}: You may put an Elemental creature card from you
- oracle_text advisory -- Fury: oracle_text diverges (similarity 0.13); scryfall='Double strike\nWhen this creature enters, it deals 4 damage divided as you choose among any number of target c
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
- oracle_text advisory -- Nourish: oracle_text diverges (similarity 0.03); scryfall='You gain 6 life.'
- oracle_text advisory -- Eternal Witness: oracle_text diverges (similarity 0.13); scryfall='When this creature enters, you may return target card from your graveyard to your hand.'
- oracle_text advisory -- Elvish Harbinger: oracle_text diverges (similarity 0.20); scryfall='When this creature enters, you may search your library for an Elf card, reveal it, then shuffle and put that c
- oracle_text advisory -- Ezuri, Renegade Leader: oracle_text diverges (similarity 0.16); scryfall='{G}: Regenerate another target Elf.\n{2}{G}{G}{G}: Elf creatures you control get +3/+3 and gain trample until 
- oracle_text advisory -- Scholar of Athreos: oracle_text diverges (similarity 0.13); scryfall='{2}{B}: Each opponent loses 1 life. You gain life equal to the life lost this way.'
- oracle_text advisory -- Brindle Boar: oracle_text diverges (similarity 0.05); scryfall='Sacrifice this creature: You gain 4 life.'
- oracle_text advisory -- Weather the Storm: oracle_text diverges (similarity 0.14); scryfall='You gain 3 life.\nStorm (When you cast this spell, copy it for each spell cast before it this turn.)'
- oracle_text advisory -- Utopia Sprawl: oracle_text diverges (similarity 0.21); scryfall='Enchant Forest\nAs this Aura enters, choose a color.\nWhenever enchanted Forest is tapped for mana, its contro
- oracle_text advisory -- Crab Umbra: oracle_text diverges (similarity 0.16); scryfall='Enchant creature\n{2}{U}: Untap enchanted creature.\nUmbra armor (If enchanted creature would be destroyed, in
- oracle_text advisory -- Staff of Domination: oracle_text diverges (similarity 0.09); scryfall='{1}: Untap this artifact.\n{2}, {T}: You gain 1 life.\n{3}, {T}: Untap target creature.\n{4}, {T}: Tap target 
- oracle_text advisory -- Galvanic Alchemist: oracle_text diverges (similarity 0.18); scryfall='Soulbond (You may pair this creature with another unpaired creature when either enters. They remain paired for
- oracle_text advisory -- Seedcradle Witch: oracle_text diverges (similarity 0.08); scryfall='{2}{G}{W}: Target creature gets +3/+3 until end of turn. Untap that creature.'
- oracle_text advisory -- Trufflesnout: oracle_text diverges (similarity 0.15); scryfall='When this creature enters, choose one —\n• Put a +1/+1 counter on this creature.\n• You gain 4 life.'
- oracle_text advisory -- Heroes' Reunion: oracle_text diverges (similarity 0.01); scryfall='Target player gains 7 life.'
- oracle_text advisory -- Rest for the Weary: oracle_text diverges (similarity 0.05); scryfall='Target player gains 4 life.\nLandfall — If you had a land enter the battlefield under your control this turn, 
- oracle_text advisory -- Accomplished Alchemist: oracle_text diverges (similarity 0.03); scryfall='{T}: Add one mana of any color.\n{T}: Add X mana of any one color, where X is the amount of life you gained th
- oracle_text advisory -- Blighted Steppe: oracle_text diverges (similarity 0.04); scryfall='{T}: Add {C}.\n{3}{W}, {T}, Sacrifice this land: You gain 2 life for each creature you control.'
- oracle_text advisory -- Wellwisher: oracle_text diverges (similarity 0.02); scryfall='{T}: You gain 1 life for each Elf on the battlefield.'
- oracle_text advisory -- Nykthos Paragon: oracle_text diverges (similarity 0.03); scryfall='Whenever you gain life, you may put that many +1/+1 counters on each creature you control. Do this only once e
- oracle_text advisory -- Blossoming Bogbeast: oracle_text diverges (similarity 0.05); scryfall='Whenever this creature attacks, you gain 2 life. Then creatures you control gain trample and get +X/+X until e
- oracle_text advisory -- Genesis Wave: oracle_text diverges (similarity 0.07); scryfall="Reveal the top X cards of your library. You may put any number of permanent cards with mana value X or less fr
- oracle_text advisory -- Champion of the Parish: oracle_text diverges (similarity 0.11); scryfall='Whenever another Human you control enters, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Field Marshal: oracle_text diverges (similarity 0.12); scryfall='Other Soldier creatures get +1/+1 and have first strike. (They deal combat damage before creatures without fir
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
- oracle_text advisory -- Moonrage Brute: oracle_text diverges (similarity 0.22); scryfall='First strike\nWard—Pay 3 life.\nNightbound (If a player casts at least two spells during their own turn, it be
- oracle_text advisory -- King Darien XLVIII: oracle_text diverges (similarity 0.17); scryfall='Other creatures you control get +1/+1.\n{3}{G}{W}: Put a +1/+1 counter on King Darien and create a 1/1 white S
- oracle_text advisory -- Fortified Beachhead: oracle_text diverges (similarity 0.26); scryfall='As this land enters, you may reveal a Soldier card from your hand. This land enters tapped unless you revealed
- oracle_text advisory -- Silent Clearing: oracle_text diverges (similarity 0.10); scryfall='{T}, Pay 1 life: Add {W} or {B}.\n{1}, {T}, Sacrifice this land: Draw a card.'
- oracle_text advisory -- Waterlogged Grove: oracle_text diverges (similarity 0.11); scryfall='{T}, Pay 1 life: Add {G} or {U}.\n{1}, {T}, Sacrifice this land: Draw a card.'
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
- oracle_text advisory -- Indrik Umbra: oracle_text diverges (similarity 0.38); scryfall='Enchant creature\nEnchanted creature gets +4/+4 and has first strike, and all creatures able to block it do so
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
- oracle_text advisory -- Mossborn Hydra: oracle_text diverges (similarity 0.13); scryfall="Trample (This creature can deal excess combat damage to the player or planeswalker it's attacking.)\nThis crea
- oracle_text advisory -- Scythecat Cub: oracle_text diverges (similarity 0.16); scryfall='Trample\nLandfall — Whenever a land you control enters, put a +1/+1 counter on target creature you control. If
- oracle_text advisory -- Scute Swarm: oracle_text diverges (similarity 0.15); scryfall="Landfall — Whenever a land you control enters, create a 1/1 green Insect creature token. If you control six or
- oracle_text advisory -- Moraug, Fury of Akoum: oracle_text diverges (similarity 0.17); scryfall="Each creature you control gets +1/+0 for each time it has attacked this turn.\nLandfall — Whenever a land you 
- oracle_text advisory -- Avenger of Zendikar: oracle_text diverges (similarity 0.21); scryfall='When this creature enters, create a 0/1 green Plant creature token for each land you control.\nLandfall — When
- oracle_text advisory -- Omnath, Locus of Rage: oracle_text diverges (similarity 0.20); scryfall='Landfall — Whenever a land you control enters, create a 5/5 red and green Elemental creature token.\nWhenever 
- oracle_text advisory -- Phylath, World Sculptor: oracle_text diverges (similarity 0.19); scryfall='When Phylath enters, create a 0/1 green Plant creature token for each basic land you control.\nLandfall — When
- oracle_text advisory -- Courser of Kruphix: oracle_text diverges (similarity 0.17); scryfall='Play with the top card of your library revealed.\nYou may play lands from the top of your library.\nLandfall —
- oracle_text advisory -- Khalni Heart Expedition: oracle_text diverges (similarity 0.19); scryfall='Landfall — Whenever a land you control enters, you may put a quest counter on this enchantment.\nRemove three 
- oracle_text advisory -- Akoum Hellhound: oracle_text diverges (similarity 0.15); scryfall='Landfall — Whenever a land you control enters, this creature gets +2/+2 until end of turn.'
- oracle_text advisory -- Brushfire Elemental: oracle_text diverges (similarity 0.26); scryfall="Haste\nThis creature can't be blocked by creatures with power 2 or less.\nLandfall — Whenever a land you contr
- oracle_text advisory -- Rampant Growth: oracle_text diverges (similarity 0.16); scryfall='Search your library for a basic land card, put that card onto the battlefield tapped, then shuffle.'
- oracle_text advisory -- Search for Tomorrow: oracle_text diverges (similarity 0.26); scryfall='Search your library for a basic land card, put it onto the battlefield, then shuffle.\nSuspend 2—{G} (Rather t
- oracle_text advisory -- Cultivate: oracle_text diverges (similarity 0.24); scryfall='Search your library for up to two basic land cards, reveal those cards, put one onto the battlefield tapped an
- oracle_text advisory -- Evolving Wilds: oracle_text diverges (similarity 0.11); scryfall='{T}, Sacrifice this land: Search your library for a basic land card, put it onto the battlefield tapped, then 
- oracle_text advisory -- Terramorphic Expanse: oracle_text diverges (similarity 0.11); scryfall='{T}, Sacrifice this land: Search your library for a basic land card, put it onto the battlefield tapped, then 
- oracle_text advisory -- Myriad Landscape: oracle_text diverges (similarity 0.25); scryfall='This land enters tapped.\n{T}: Add {C}.\n{2}, {T}, Sacrifice this land: Search your library for up to two basi
- oracle_text advisory -- Primeval Titan: oracle_text diverges (similarity 0.16); scryfall='Trample\nWhenever this creature enters or attacks, you may search your library for up to two land cards, put t
- oracle_text advisory -- Radiant Fountain: oracle_text diverges (similarity 0.12); scryfall='When this land enters, you gain 2 life.\n{T}: Add {C}.'
- oracle_text advisory -- Haunted Fengraf: oracle_text diverges (similarity 0.12); scryfall='{T}: Add {C}.\n{3}, {T}, Sacrifice this land: Return a creature card at random from your graveyard to your han
- oracle_text advisory -- Temple of Abandon: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Deceit: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Enlightenment: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Malady: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Malice: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Mystery: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Plenty: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Triumph: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Zhalfirin Void: oracle_text diverges (similarity 0.74); scryfall='When this land enters, scry 1. (Look at the top card of your library. You may put that card on the bottom.)\n{
- oracle_text advisory -- Crystal Grotto: oracle_text diverges (similarity 0.39); scryfall='When this land enters, scry 1.\n{T}: Add {C}.\n{1}, {T}: Add one mana of any color.'
- oracle_text advisory -- Rumble Arena: oracle_text diverges (similarity 0.44); scryfall='Vigilance\nWhen this land enters, scry 1. (Look at the top card of your library. You may put it on the bottom.
- oracle_text advisory -- The Grey Havens: oracle_text diverges (similarity 0.24); scryfall='When The Grey Havens enters, scry 1.\n{T}: Add {C}.\n{T}: Add one mana of any color among legendary creature c
- oracle_text advisory -- Galadriel of Lothlórien: oracle_text diverges (similarity 0.24); scryfall='Whenever the Ring tempts you, if you chose a creature other than Galadriel as your Ring-bearer, scry 3.\nWhene
- oracle_text advisory -- Clifftop Retreat: oracle_text diverges (similarity 0.37); scryfall='This land enters tapped unless you control a Mountain or a Plains.\n{T}: Add {R} or {W}.'
- oracle_text advisory -- Furycalm Snarl: oracle_text diverges (similarity 0.31); scryfall="As this land enters, you may reveal a Mountain or Plains card from your hand. If you don't, this land enters t
- oracle_text advisory -- Steelshaper's Gift: oracle_text diverges (similarity 0.18); scryfall='Search your library for an Equipment card, reveal that card, put it into your hand, then shuffle.'
- oracle_text advisory -- Swiftblade Vindicator: oracle_text diverges (similarity 0.27); scryfall="Double strike (This creature deals both first-strike and regular combat damage.)\nVigilance (Attacking doesn't
- oracle_text advisory -- Lizard Blades: oracle_text diverges (similarity 0.11); scryfall="Double strike\nEquipped creature has double strike.\nReconfigure {2} ({2}: Attach to target creature you contr
- oracle_text advisory -- Magnetic Theft: oracle_text diverges (similarity 0.04); scryfall="Attach target Equipment to target creature. (Control of the Equipment doesn't change.)"
- oracle_text advisory -- Forge Anew: oracle_text diverges (similarity 0.09); scryfall='When this enchantment enters, return target Equipment card from your graveyard to the battlefield.\nDuring you
- oracle_text advisory -- Raubahn, Bull of Ala Mhigo: oracle_text diverges (similarity 0.07); scryfall="Ward—Pay life equal to Raubahn's power.\nWhenever Raubahn attacks, attach up to one target Equipment you contr
- oracle_text advisory -- Speaker of the Heavens: oracle_text diverges (similarity 0.16); scryfall="Vigilance, lifelink (Attacking doesn't cause this creature to tap. Damage dealt by this creature also causes y
- oracle_text advisory -- Path to Exile: oracle_text diverges (similarity 0.12); scryfall='Exile target creature. Its controller may search their library for a basic land card, put that card onto the b
- oracle_text advisory -- Sulfur Falls: oracle_text diverges (similarity 0.27); scryfall='This land enters tapped unless you control an Island or a Mountain.\n{T}: Add {U} or {R}.'
- oracle_text advisory -- Gitaxian Probe: oracle_text diverges (similarity 0.16); scryfall="({U/P} can be paid with either {U} or 2 life.)\nLook at target player's hand.\nDraw a card."
- oracle_text advisory -- Generator Servant: oracle_text diverges (similarity 0.15); scryfall='{T}, Sacrifice this creature: Add {C}{C}. If any of that mana is spent on a creature spell, it gains haste unt
- oracle_text advisory -- Brainstorm: oracle_text diverges (similarity 0.07); scryfall='Draw three cards, then put two cards from your hand on top of your library in any order.'
- oracle_text advisory -- Legion Lieutenant: oracle_text diverges (similarity 0.11); scryfall='Other Vampires you control get +1/+1.'
- oracle_text advisory -- Vampire of the Dire Moon: oracle_text diverges (similarity 0.37); scryfall='Deathtouch (Any amount of damage this deals to a creature is enough to destroy it.)\nLifelink (Damage dealt by
- oracle_text advisory -- Falkenrath Pit Fighter: oracle_text diverges (similarity 0.11); scryfall='{1}{R}, Discard a card, Sacrifice a Vampire: Draw two cards. Activate only if an opponent lost life this turn.
- oracle_text advisory -- Stromkirk Condemned: oracle_text diverges (similarity 0.15); scryfall='Discard a card: Vampires you control get +1/+1 until end of turn. Activate only once each turn.'
- oracle_text advisory -- Voldaren Estate: oracle_text diverges (similarity 0.35); scryfall='{T}: Add {C}.\n{T}, Pay 1 life: Add one mana of any color. Spend this mana only to cast a Vampire spell.\n{5},
- oracle_text advisory -- Bloodghast: oracle_text diverges (similarity 0.19); scryfall="This creature can't block.\nThis creature has haste as long as an opponent has 10 or less life.\nLandfall — Wh
- oracle_text advisory -- Stromkirk Captain: oracle_text diverges (similarity 0.11); scryfall='First strike\nOther Vampire creatures you control get +1/+1 and have first strike.'
- oracle_text advisory -- Drana, Liberator of Malakir: oracle_text diverges (similarity 0.14); scryfall='Flying, first strike\nWhenever Drana deals combat damage to a player, put a +1/+1 counter on each attacking cr
- oracle_text advisory -- Mavren Fein, Dusk Apostle: oracle_text diverges (similarity 0.14); scryfall='Whenever one or more nontoken Vampires you control attack, create a 1/1 white Vampire creature token with life
- oracle_text advisory -- Vampire Socialite: oracle_text diverges (similarity 0.26); scryfall="Menace (This creature can't be blocked except by two or more creatures.)\nWhen this creature enters, if an opp
- oracle_text advisory -- Falkenrath Gorger: oracle_text diverges (similarity 0.27); scryfall="Each Vampire creature card you own that isn't on the battlefield has madness. The madness cost is equal to its
- oracle_text advisory -- Markov Baron: oracle_text diverges (similarity 0.26); scryfall="Convoke (Your creatures can help cast this spell. Each creature you tap while casting this spell pays for {1} 
- oracle_text advisory -- Asylum Visitor: oracle_text diverges (similarity 0.33); scryfall="At the beginning of each player's upkeep, if that player has no cards in hand, you draw a card and you lose 1 
- oracle_text advisory -- Captivating Vampire: oracle_text diverges (similarity 0.12); scryfall='Other Vampire creatures you control get +1/+1.\nTap five untapped Vampires you control: Gain control of target
- oracle_text advisory -- Bloodline Keeper: oracle_text diverges (similarity 0.12); scryfall='Flying\n{T}: Create a 2/2 black Vampire creature token with flying.\n{B}: Transform this creature. Activate on
- oracle_text advisory -- Lord of Lineage: oracle_text diverges (similarity 0.17); scryfall='Flying\nOther Vampire creatures you control get +2/+2.\n{T}: Create a 2/2 black Vampire creature token with fl
- oracle_text advisory -- Kessig Wolf Run: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {C}.\n{X}{R}{G}, {T}: Target creature gets +X/+0 and gains trample until end of turn.'
- oracle_text advisory -- Harrow: oracle_text diverges (similarity 0.19); scryfall='As an additional cost to cast this spell, sacrifice a land.\nSearch your library for up to two basic land card
- oracle_text advisory -- Explosive Vegetation: oracle_text diverges (similarity 0.29); scryfall='Search your library for up to two basic land cards, put them onto the battlefield tapped, then shuffle.'
- oracle_text advisory -- Nature's Lore: oracle_text diverges (similarity 0.19); scryfall='Search your library for a Forest card, put that card onto the battlefield, then shuffle.'
- oracle_text advisory -- Three Visits: oracle_text diverges (similarity 0.18); scryfall='Search your library for a Forest card, put it onto the battlefield, then shuffle.'
- oracle_text advisory -- Vernal Bloom: oracle_text diverges (similarity 0.14); scryfall='Whenever a Forest is tapped for mana, its controller adds an additional {G}.'
- oracle_text advisory -- Gauntlet of Power: oracle_text diverges (similarity 0.19); scryfall='As this artifact enters, choose a color.\nCreatures of the chosen color get +1/+1.\nWhenever a basic land is t
- oracle_text advisory -- Nissa, Who Shakes the World: oracle_text diverges (similarity 0.25); scryfall='Whenever you tap a Forest for mana, add an additional {G}.\n+1: Put three +1/+1 counters on up to one target n
- oracle_text advisory -- Kalonian Hydra: oracle_text diverges (similarity 0.15); scryfall='Trample\nThis creature enters with four +1/+1 counters on it.\nWhenever this creature attacks, double the numb
- oracle_text advisory -- Garruk, Primal Hunter: oracle_text diverges (similarity 0.20); scryfall='+1: Create a 3/3 green Beast creature token.\n−3: Draw cards equal to the greatest power among creatures you c
- oracle_text advisory -- Primordial Hydra: oracle_text diverges (similarity 0.24); scryfall='This creature enters with X +1/+1 counters on it.\nAt the beginning of your upkeep, double the number of +1/+1
- oracle_text advisory -- Voracious Hydra: oracle_text diverges (similarity 0.21); scryfall="Trample\nThis creature enters with X +1/+1 counters on it.\nWhen this creature enters, choose one —\n• Double 
- oracle_text advisory -- Neverwinter Hydra: oracle_text diverges (similarity 0.29); scryfall='As this creature enters, roll X d6. It enters with a number of +1/+1 counters on it equal to the total of thos
- oracle_text advisory -- Steelbane Hydra: oracle_text diverges (similarity 0.16); scryfall='This creature enters with X +1/+1 counters on it.\n{2}{G}, Remove a +1/+1 counter from this creature: Destroy 
- oracle_text advisory -- Gargos, Vicious Watcher: oracle_text diverges (similarity 0.23); scryfall="Vigilance\nHydra spells you cast cost {4} less to cast.\nWhenever a creature you control becomes the target of
- oracle_text advisory -- Skyshroud Claim: oracle_text diverges (similarity 0.26); scryfall='Search your library for up to two Forest cards, put them onto the battlefield, then shuffle.'
- oracle_text advisory -- Martyr of Sands: oracle_text diverges (similarity 0.06); scryfall='{1}, Reveal X white cards from your hand, Sacrifice this creature: You gain three times X life.'
- oracle_text advisory -- Walking Ballista: oracle_text diverges (similarity 0.06); scryfall='This creature enters with X +1/+1 counters on it.\n{4}: Put a +1/+1 counter on this creature.\nRemove a +1/+1 
- oracle_text advisory -- Abiding Grace: oracle_text diverges (similarity 0.11); scryfall='At the beginning of your end step, choose one —\n• You gain 1 life.\n• Return target creature card with mana v
- oracle_text advisory -- Emeria, the Sky Ruin: oracle_text diverges (similarity 0.19); scryfall='This land enters tapped.\nAt the beginning of your upkeep, if you control seven or more Plains, you may return
- oracle_text advisory -- Brainsurge: oracle_text diverges (similarity 0.09); scryfall='Draw four cards, then put two cards from your hand on top of your library in any order.'
- oracle_text advisory -- Street Wraith: oracle_text diverges (similarity 0.26); scryfall="Swampwalk (This creature can't be blocked as long as defending player controls a Swamp.)\nCycling—Pay 2 life. 
- oracle_text advisory -- Unpredictable Cyclone: oracle_text diverges (similarity 0.31); scryfall="If a cycling ability of another nonland card would cause you to draw a card, instead exile cards from the top 
- oracle_text advisory -- Curator of Mysteries: oracle_text diverges (similarity 0.16); scryfall='Flying\nWhenever you cycle or discard another card, scry 1.\nCycling {U} ({U}, Discard this card: Draw a card.
- oracle_text advisory -- Emrakul, the Aeons Torn: oracle_text diverges (similarity 0.17); scryfall="This spell can't be countered.\nWhen you cast this spell, take an extra turn after this one.\nFlying, protecti
- oracle_text advisory -- Ulamog, the Ceaseless Hunger: oracle_text diverges (similarity 0.15); scryfall='When you cast this spell, exile two target permanents.\nIndestructible\nWhenever Ulamog attacks, defending pla
- oracle_text advisory -- Ulamog, the Infinite Gyre: oracle_text diverges (similarity 0.31); scryfall='When you cast this spell, destroy target permanent.\nIndestructible\nAnnihilator 4 (Whenever this creature att
- oracle_text advisory -- Long-Term Plans: oracle_text diverges (similarity 0.10); scryfall='Search your library for a card, then shuffle and put that card third from the top.'
- oracle_text advisory -- Toxic Deluge: oracle_text diverges (similarity 0.13); scryfall='As an additional cost to cast this spell, pay X life.\nAll creatures get -X/-X until end of turn.'
- oracle_text advisory -- Lord of Atlantis: oracle_text diverges (similarity 0.47); scryfall="Other Merfolk get +1/+1 and have islandwalk. (They can't be blocked as long as defending player controls an Is
- oracle_text advisory -- Master of the Pearl Trident: oracle_text diverges (similarity 0.63); scryfall="Other Merfolk creatures you control get +1/+1 and have islandwalk. (They can't be blocked as long as defending
- oracle_text advisory -- Vodalian Hexcatcher: oracle_text diverges (similarity 0.39); scryfall='Flash\nOther Merfolk you control get +1/+1.\nSacrifice a Merfolk: Counter target noncreature spell unless its 
- oracle_text advisory -- Phantasmal Image: oracle_text diverges (similarity 0.58); scryfall='You may have this creature enter as a copy of any creature on the battlefield, except it\'s an Illusion in add
- oracle_text advisory -- Cosi's Trickster: oracle_text diverges (similarity 0.38); scryfall='Whenever an opponent shuffles their library, you may put a +1/+1 counter on this creature.'
- oracle_text advisory -- Sink into Stupor: oracle_text diverges (similarity 0.58); scryfall="Return target spell or nonland permanent an opponent controls to its owner's hand."
- oracle_text advisory -- Dark Ritual: oracle_text diverges (similarity 0.04); scryfall='Add {B}{B}{B}.'
- oracle_text advisory -- Demonic Tutor: oracle_text diverges (similarity 0.21); scryfall='Search your library for a card, put that card into your hand, then shuffle.'
- oracle_text advisory -- Diabolic Intent: oracle_text diverges (similarity 0.25); scryfall='As an additional cost to cast this spell, sacrifice a creature.\nSearch your library for a card, put that card
- oracle_text advisory -- Culling the Weak: oracle_text diverges (similarity 0.13); scryfall='As an additional cost to cast this spell, sacrifice a creature.\nAdd {B}{B}{B}{B}.'
- oracle_text advisory -- Lake of the Dead: oracle_text diverges (similarity 0.14); scryfall="If this land would enter, sacrifice a Swamp instead. If you do, put this land onto the battlefield. If you don
- oracle_text advisory -- Profane Tutor: oracle_text diverges (similarity 0.27); scryfall='Suspend 2—{1}{B} (Rather than cast this card from your hand, pay {1}{B} and exile it with two time counters on
- oracle_text advisory -- Shadowborn Apostle: oracle_text diverges (similarity 0.17); scryfall='A deck can have any number of cards named Shadowborn Apostle.\n{B}, Sacrifice six creatures named Shadowborn A
- oracle_text advisory -- Abhorrent Overlord: oracle_text diverges (similarity 0.24); scryfall='Flying\nWhen this creature enters, create a number of 1/1 black Harpy creature tokens with flying equal to you
- oracle_text advisory -- Reaper from the Abyss: oracle_text diverges (similarity 0.15); scryfall='Flying\nMorbid — At the beginning of each end step, if a creature died this turn, destroy target non-Demon cre
- oracle_text advisory -- Sire of Insanity: oracle_text diverges (similarity 0.14); scryfall='At the beginning of each end step, each player discards their hand.'
- oracle_text advisory -- Jetmir, Nexus of Revels: oracle_text diverges (similarity 0.40); scryfall='Creatures you control get +1/+0 and have vigilance as long as you control three or more creatures.\nCreatures 
- oracle_text advisory -- Thrumming Stone: oracle_text diverges (similarity 0.17); scryfall='Spells you cast have ripple 4. (Whenever you cast a spell, you may reveal the top four cards of your library. 
- oracle_text advisory -- Chariot of Victory: oracle_text diverges (similarity 0.45); scryfall='Equipped creature has first strike, trample, and haste.\nEquip {1}'
- oracle_text advisory -- Voyaging Satyr: oracle_text diverges (similarity 0.13); scryfall='{T}: Untap target land.'
- oracle_text advisory -- Surrak and Goreclaw: oracle_text diverges (similarity 0.63); scryfall='Trample\nOther creatures you control have trample.\nWhenever another nontoken creature you control enters, put
- oracle_text advisory -- Nylea, God of the Hunt: oracle_text diverges (similarity 0.54); scryfall="Indestructible\nAs long as your devotion to green is less than five, Nylea isn't a creature. (Each {G} in the 
- oracle_text advisory -- Extraplanar Lens: oracle_text diverges (similarity 0.51); scryfall='Imprint — When this artifact enters, you may exile target land you control.\nWhenever a land with the same nam
- oracle_text advisory -- Horn of Greed: oracle_text diverges (similarity 0.27); scryfall='Whenever a player plays a land, that player draws a card.'
- oracle_text advisory -- Howling Mine: oracle_text diverges (similarity 0.37); scryfall="At the beginning of each player's draw step, if this artifact is untapped, that player draws an additional car
- oracle_text advisory -- Thraben Inspector: oracle_text diverges (similarity 0.26); scryfall='When this creature enters, investigate. (Create a Clue token. It\'s an artifact with "{2}, Sacrifice this toke
- oracle_text advisory -- Squadron Hawk: oracle_text diverges (similarity 0.19); scryfall='Flying\nWhen this creature enters, you may search your library for up to three cards named Squadron Hawk, reve
- oracle_text advisory -- Curious Farm Animals: oracle_text diverges (similarity 0.10); scryfall='When this creature dies, you gain 3 life.\n{2}, Sacrifice this creature: Destroy up to one target artifact or 
- oracle_text advisory -- Kami of False Hope: oracle_text diverges (similarity 0.09); scryfall='Sacrifice this creature: Prevent all combat damage that would be dealt this turn.'
- oracle_text advisory -- Enduring Innocence: oracle_text diverges (similarity 0.20); scryfall="Lifelink\nWhenever one or more other creatures you control with power 2 or less enter, draw a card. This abili
- oracle_text advisory -- Lurrus of the Dream-Den: oracle_text diverges (similarity 0.23); scryfall='Companion — Each permanent card in your starting deck has mana value 2 or less. (If this card is your chosen c
- oracle_text advisory -- Careful Study: oracle_text diverges (similarity 0.27); scryfall='Draw two cards, then discard two cards.'
- oracle_text advisory -- Faithless Looting: oracle_text diverges (similarity 0.59); scryfall='Draw two cards, then discard two cards.\nFlashback {2}{R} (You may cast this card from your graveyard for its 
- oracle_text advisory -- Purphoros, God of the Forge: oracle_text diverges (similarity 0.60); scryfall="Indestructible\nAs long as your devotion to red is less than five, Purphoros isn't a creature.\nWhenever anoth
- oracle_text advisory -- Mystic Retrieval: oracle_text diverges (similarity 0.49); scryfall='Return target instant or sorcery card from your graveyard to your hand.\nFlashback {2}{R} (You may cast this c
- oracle_text advisory -- Magus of the Bazaar: oracle_text diverges (similarity 0.22); scryfall='{T}: Draw two cards, then discard three cards.'
- oracle_text advisory -- Cephalid Coliseum: oracle_text diverges (similarity 0.62); scryfall='{T}: Add {U}. This land deals 1 damage to you.\nThreshold — {U}, {T}, Sacrifice this land: Target player draws
- oracle_text advisory -- Golgari Grave-Troll: oracle_text diverges (similarity 0.53); scryfall='This creature enters with a +1/+1 counter on it for each creature card in your graveyard.\n{1}, Remove a +1/+1
- oracle_text advisory -- Stinkweed Imp: oracle_text diverges (similarity 0.64); scryfall='Flying\nWhenever this creature deals combat damage to a creature, destroy that creature.\nDredge 5 (If you wou
- oracle_text advisory -- Life from the Loam: oracle_text diverges (similarity 0.68); scryfall='Return up to three target land cards from your graveyard to your hand.\nDredge 3 (If you would draw a card, yo
- oracle_text advisory -- Keranos, God of Storms: oracle_text diverges (similarity 0.58); scryfall="Indestructible\nAs long as your devotion to blue and red is less than seven, Keranos isn't a creature.\nReveal
- oracle_text advisory -- Mogis, God of Slaughter: oracle_text diverges (similarity 0.58); scryfall="Indestructible\nAs long as your devotion to black and red is less than seven, Mogis isn't a creature.\nAt the 
- oracle_text advisory -- Klothys, God of Destiny: oracle_text diverges (similarity 0.65); scryfall="Indestructible\nAs long as your devotion to red and green is less than seven, Klothys isn't a creature.\nAt th
- oracle_text advisory -- Ephara, God of the Polis: oracle_text diverges (similarity 0.57); scryfall="Indestructible\nAs long as your devotion to white and blue is less than seven, Ephara isn't a creature.\nAt th
- oracle_text advisory -- Phenax, God of Deception: oracle_text diverges (similarity 0.65); scryfall='Indestructible\nAs long as your devotion to blue and black is less than seven, Phenax isn\'t a creature.\nCrea
- oracle_text advisory -- Wild Defiance: oracle_text diverges (similarity 0.21); scryfall='Whenever a creature you control becomes the target of an instant or sorcery spell, that creature gets +3/+3 un
- oracle_text advisory -- Berserk: oracle_text diverges (similarity 0.25); scryfall='Cast this spell only before the combat damage step.\nTarget creature gains trample and gets +X/+0 until end of
- oracle_text advisory -- Temur Battle Rage: oracle_text diverges (similarity 0.33); scryfall='Target creature gains double strike until end of turn.\nFerocious — That creature also gains trample until end
- oracle_text advisory -- Might of Old Krosa: oracle_text diverges (similarity 0.33); scryfall='Target creature gets +2/+2 until end of turn. If you cast this spell during your main phase, that creature get
- oracle_text advisory -- Vines of Vastwood: oracle_text diverges (similarity 0.44); scryfall="Kicker {G} (You may pay an additional {G} as you cast this spell.)\nTarget creature can't be the target of spe
- oracle_text advisory -- Legion Leadership: oracle_text diverges (similarity 0.11); scryfall="Until end of turn, double target creature's power and it gains first strike."
- oracle_text advisory -- Spellskite: oracle_text diverges (similarity 0.18); scryfall='{U/P}: Change a target of target spell or ability to this creature. ({U/P} can be paid with either {U} or 2 li
- oracle_text advisory -- Blistering Firecat: oracle_text diverges (similarity 0.21); scryfall='Trample, haste\nAt the beginning of the end step, sacrifice this creature.\nMorph {R}{R} (You may cast this ca
- oracle_text advisory -- Hystrodon: oracle_text diverges (similarity 0.31); scryfall='Trample\nWhenever this creature deals combat damage to a player, you may draw a card.\nMorph {1}{G}{G} (You ma
- oracle_text advisory -- Den Protector: oracle_text diverges (similarity 0.37); scryfall="Creatures with power less than this creature's power can't block it.\nMegamorph {1}{G} (You may cast this card
- oracle_text advisory -- Fire-Lit Thicket: oracle_text diverges (similarity 0.36); scryfall='{T}: Add {C}.\n{R/G}, {T}: Add {R}{R}, {R}{G}, or {G}{G}.'
- oracle_text advisory -- Drogskol Captain: oracle_text diverges (similarity 0.25); scryfall="Flying\nOther Spirit creatures you control get +1/+1 and have hexproof. (They can't be the targets of spells o
- oracle_text advisory -- Supreme Phantom: oracle_text diverges (similarity 0.17); scryfall='Flying\nOther Spirits you control get +1/+1.'
- oracle_text advisory -- Shacklegeist: oracle_text diverges (similarity 0.17); scryfall="Flying\nThis creature can block only creatures with flying.\nTap two untapped Spirits you control: Tap target 
- oracle_text advisory -- Spirit of the Labyrinth: oracle_text diverges (similarity 0.08); scryfall="Each player can't draw more than one card each turn."
- oracle_text advisory -- Flooded Strand: oracle_text diverges (similarity 0.14); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Plains or Island card, put it onto the battlef
- oracle_text advisory -- Geist of Saint Traft: oracle_text diverges (similarity 0.23); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)\nWhenever Geist of
- oracle_text advisory -- Lingering Souls: oracle_text diverges (similarity 0.16); scryfall='Create two 1/1 white Spirit creature tokens with flying.\nFlashback {1}{B} (You may cast this card from your g
- oracle_text advisory -- Patrician Geist: oracle_text diverges (similarity 0.14); scryfall='Flying\nOther Spirits you control get +1/+1.\nSpells you cast from your graveyard cost {1} less to cast.'
- oracle_text advisory -- Mausoleum Wanderer: oracle_text diverges (similarity 0.19); scryfall="Flying\nWhenever another Spirit you control enters, this creature gets +1/+1 until end of turn.\nSacrifice thi
- oracle_text advisory -- Skyclave Apparition: oracle_text diverges (similarity 0.28); scryfall="When this creature enters, exile up to one target nonland, nontoken permanent you don't control with mana valu
- oracle_text advisory -- White Orchid Phantom: oracle_text diverges (similarity 0.21); scryfall='Flying, first strike\nWhen this creature enters, destroy up to one target nonbasic land. Its controller may se
- oracle_text advisory -- Usher of the Fallen: oracle_text diverges (similarity 0.09); scryfall='Boast — {1}{W}: Create a 1/1 white Human Warrior creature token. (Activate only if this creature attacked this
- oracle_text advisory -- Millicent, Restless Revenant: oracle_text diverges (similarity 0.16); scryfall='Affinity for Spirits (This spell costs {1} less to cast for each Spirit you control.)\nFlying\nWhenever Millic
- oracle_text advisory -- Zhur-Taa Druid: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {G}.\nWhenever you tap this creature for mana, it deals 1 damage to each opponent.'
- oracle_text advisory -- Spark Elemental: oracle_text diverges (similarity 0.46); scryfall="Trample, haste (This creature can deal excess combat damage to the player or planeswalker it's attacking. This
- oracle_text advisory -- Ball Lightning: oracle_text diverges (similarity 0.53); scryfall="Trample (This creature can deal excess combat damage to the player or planeswalker it's attacking.)\nHaste (Th
- oracle_text advisory -- Flamekin Bladewhirl: oracle_text diverges (similarity 0.12); scryfall='As an additional cost to cast this spell, reveal an Elemental card from your hand or pay {3}.'
- oracle_text advisory -- Sunspine Lynx: oracle_text diverges (similarity 0.19); scryfall="Players can't gain life.\nDamage can't be prevented.\nWhen this creature enters, it deals damage to each playe
- oracle_text advisory -- Hellspark Elemental: oracle_text diverges (similarity 0.16); scryfall='Trample, haste\nAt the beginning of the end step, sacrifice this creature.\nUnearth {1}{R} ({1}{R}: Return thi
- oracle_text advisory -- Nova Chaser: oracle_text diverges (similarity 0.12); scryfall='Trample\nChampion an Elemental (When this enters, sacrifice it unless you exile another Elemental you control.
- oracle_text advisory -- Thunderkin Awakener: oracle_text diverges (similarity 0.17); scryfall="Haste\nWhenever this creature attacks, choose target Elemental creature card in your graveyard with toughness 
- oracle_text advisory -- Maze's End: oracle_text diverges (similarity 0.30); scryfall="This land enters tapped.\n{T}: Add {C}.\n{3}, {T}, Return this land to its owner's hand: Search your library f
- oracle_text advisory -- Azorius Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {W} or {U}.'
- oracle_text advisory -- Boros Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {R} or {W}.'
- oracle_text advisory -- Dimir Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {U} or {B}.'
- oracle_text advisory -- Golgari Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {B} or {G}.'
- oracle_text advisory -- Gruul Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {R} or {G}.'
- oracle_text advisory -- Izzet Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {U} or {R}.'
- oracle_text advisory -- Orzhov Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {W} or {B}.'
- oracle_text advisory -- Rakdos Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {B} or {R}.'
- oracle_text advisory -- Selesnya Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {G} or {W}.'
- oracle_text advisory -- Simic Guildgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\n{T}: Add {G} or {U}.'
- oracle_text advisory -- Planar Nexus: oracle_text diverges (similarity 0.39); scryfall="This land is every nonbasic land type. (Nonbasic land types include Cave, Desert, Gate, Lair, Locus, Mine, Pow
- oracle_text advisory -- Guild Summit: oracle_text diverges (similarity 0.35); scryfall='When this enchantment enters, you may tap any number of untapped Gates you control. Draw a card for each Gate 
- oracle_text advisory -- Gates Ablaze: oracle_text diverges (similarity 0.28); scryfall='Gates Ablaze deals X damage to each creature, where X is the number of Gates you control.'
- oracle_text advisory -- Archdruid's Charm: oracle_text diverges (similarity 0.45); scryfall="Choose one —\n• Search your library for a creature or land card and reveal it. Put it onto the battlefield tap
- oracle_text advisory -- Gond Gate: oracle_text diverges (similarity 0.19); scryfall='Gates you control enter untapped.\n{T}: Add {C}.\n{T}: Add one mana of any color that a Gate you control could
- oracle_text advisory -- Exploration: oracle_text diverges (similarity 0.13); scryfall='You may play an additional land on each of your turns.'
- oracle_text advisory -- Burgeoning: oracle_text diverges (similarity 0.20); scryfall='Whenever an opponent plays a land, you may put a land card from your hand onto the battlefield.'
- oracle_text advisory -- Manabond: oracle_text diverges (similarity 0.32); scryfall='At the beginning of your end step, you may reveal your hand and put all land cards from it onto the battlefiel
- oracle_text advisory -- Amulet of Vigor: oracle_text diverges (similarity 0.10); scryfall='Whenever a permanent you control enters tapped, untap it.'
- oracle_text advisory -- Thran Portal: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped unless you control two or fewer other lands.\nAs this land enters, choose a basic land
- oracle_text advisory -- Gateway Plaza: oracle_text diverges (similarity 0.24); scryfall='This land enters tapped.\nWhen this land enters, sacrifice it unless you pay {1}.\n{T}: Add one mana of any co
- oracle_text advisory -- Sea Gate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\nAs this land enters, choose a color other than blue.\n{T}: Add {U} or one mana of th
- oracle_text advisory -- Manor Gate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\nAs this land enters, choose a color other than green.\n{T}: Add {G} or one mana of t
- oracle_text advisory -- Cliffgate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\nAs this land enters, choose a color other than red.\n{T}: Add {R} or one mana of the
- oracle_text advisory -- Citadel Gate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\nAs this land enters, choose a color other than white.\n{T}: Add {W} or one mana of t
- oracle_text advisory -- Black Dragon Gate: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.\nAs this land enters, choose a color other than black.\n{T}: Add {B} or one mana of t
- oracle_text advisory -- The Black Gate: oracle_text diverges (similarity 0.37); scryfall="As The Black Gate enters, you may pay 3 life. If you don't, it enters tapped.\n{T}: Add {B}.\n{1}{B}, {T}: Cho
- oracle_text advisory -- Heap Gate: oracle_text diverges (similarity 0.29); scryfall='{T}: Add {C}.\n{1}, {T}: Add one mana of any color.\n{1}, {T}, Tap an untapped Gate you control: Create a Trea
- oracle_text advisory -- Baldur's Gate: oracle_text diverges (similarity 0.22); scryfall='{T}: Add {C}.\n{2}, {T}: Add X mana of any one color, where X is the number of other Gates you control.'
- oracle_text advisory -- Basilisk Gate: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.\n{2}, {T}: Target creature gets +X/+X until end of turn, where X is the number of Gates you cont
- oracle_text advisory -- Talon Gates of Madara: oracle_text diverges (similarity 0.20); scryfall='When this land enters, up to one target creature phases out.\n{T}: Add {C}.\n{1}, {T}: Add one mana of any col
- clause_ledger: no dedicated per-clause artifact. Its function -- every oracle clause modeled/inert/deferred -- is covered by coverage(partial hard-stop) + bracket-note deferrals + viewer oracle cross-check + audit_card_fields oracle-diff. A dedicated ledger is deferred (high per-card cost, marginal added rigor).
- play advisory -- seed 7001 gi 0: turn 6 plan 0: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 24: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 48: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 72: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 96: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 120: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 144: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 168: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 192: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 216: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 240: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 264: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 288: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 312: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 336: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 360: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 390: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 396: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 420: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 426: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 1152: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 1176: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 1212: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 1218: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 1220: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 1222: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 1228: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 1234: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 1776: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 1800: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 1836: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 1842: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 1844: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 1846: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 1852: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 1858: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 2406: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 2412: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 2436: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 2442: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 2454: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 2460: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 2484: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 2490: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 2496: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 2529: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 2531: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 2533: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 2534: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 2552: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 2800: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 2836: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 2842: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 2866: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 2872: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 2884: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 2890: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 2914: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 2920: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 2983: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 2985: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 2987: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 2988: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 3344: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 3350: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 3352: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 3354: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 3360: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 3366: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 3380: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 3386: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 3388: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 3390: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 3396: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 3402: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 3444: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 3730: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 3736: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 3738: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 3740: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 3746: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 3752: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 3766: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 3772: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 3774: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 3776: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 3782: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 3788: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 3830: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 4227: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 4229: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 4231: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 4232: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 4243: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 4245: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 4247: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 4248: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 6 plan 4879: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 0: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 34: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 35: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 68: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 69: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 102: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 103: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 136: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 137: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 170: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 171: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 204: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 205: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 238: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 239: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 272: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 273: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 306: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 307: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 340: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 341: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 374: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 375: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 408: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 409: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 442: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 443: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 476: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 477: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 510: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 511: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 544: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 545: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 578: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 579: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 612: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 613: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 646: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 647: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 680: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 681: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 714: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 715: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 748: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 749: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 782: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 783: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 816: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 817: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 850: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 851: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 884: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 885: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 918: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 919: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 952: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 953: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 986: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 987: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1021: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1039: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1057: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1075: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1093: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1111: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1129: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1147: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1165: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1183: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1201: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1219: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1237: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1255: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1273: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1291: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1309: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1327: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1345: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1363: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1381: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1399: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1417: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1435: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1453: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1471: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1489: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1507: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1525: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1543: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1560: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1561: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1595: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1596: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1643: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1644: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1677: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1678: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1712: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1713: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1760: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1761: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1795: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1813: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1831: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1867: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1885: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 7 plan 1903: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 0: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 1: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 2: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 3: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 4: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 5: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 6: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 7: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 8: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 9: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 10: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 11: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 12: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 13: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 14: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 15: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 16: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 17: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 18: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 19: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 20: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 21: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 22: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 23: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 24: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 25: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 26: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 27: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 28: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 29: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 30: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 31: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 32: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 33: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 34: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 35: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 36: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 37: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 38: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 39: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 40: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 41: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 42: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 43: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 44: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 45: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 46: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 47: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 48: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 49: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 50: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 51: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 52: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 53: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 54: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 55: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 56: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 57: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 58: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 59: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 60: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 61: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 62: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 63: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 80: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 124: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 260: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 396: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 792: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 1188: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 1584: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 1980: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 2376: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 2772: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 2896: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 3056: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 3100: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 3108: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 3212: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 3804: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 3948: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 5124: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 6300: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 7476: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 8652: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 9828: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 11004: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 12156: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 13308: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 14460: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 15612: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 16764: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 17916: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 21921: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 22272: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 22623: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 22974: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 23325: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 23676: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 24027: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 25731: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 26784: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 27837: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 28890: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 29943: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 30996: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 32049: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 35163: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 36216: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 37269: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 38322: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 39375: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 40428: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 41481: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 42534: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 42687: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 42840: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 43317: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 43470: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 43623: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 43776: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 43929: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 43930: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 44280: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 44631: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 44982: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 45333: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 45684: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 0: turn 8 plan 46035: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 0: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 24: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 48: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 72: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 96: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 120: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 144: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 168: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 192: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 216: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 240: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 264: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 288: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 312: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 336: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 360: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 390: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 396: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 420: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 426: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 1152: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 1176: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 1212: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 1218: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 1220: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 1222: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 1228: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 1234: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 1776: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 1800: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 1836: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 1842: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 1844: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 1846: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 1852: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 1858: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 2406: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 2412: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 2436: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 2442: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 2454: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 2460: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 2484: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 2490: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 2496: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 2529: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 2531: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 2533: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 2534: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 2552: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 2800: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 2836: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 2842: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 2866: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 2872: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 2884: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 2890: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 2914: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 2920: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 2983: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 2985: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 2987: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 2988: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 3344: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 3350: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 3352: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 3354: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 3360: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 3366: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 3380: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 3386: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 3388: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 3390: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 3396: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 3402: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 3444: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 3730: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 3736: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 3738: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 3740: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 3746: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 3752: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 3766: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 3772: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 3774: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 3776: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 3782: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 3788: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 3830: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 4227: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 4229: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 4231: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 4232: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 4243: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 4245: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 4247: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 4248: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 6 plan 4879: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 0: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 34: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 35: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 68: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 69: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 102: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 103: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 136: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 137: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 170: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 171: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 204: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 205: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 238: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 239: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 272: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 273: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 306: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 307: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 340: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 341: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 374: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 375: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 408: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 409: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 442: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 443: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 476: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 477: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 510: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 511: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 544: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 545: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 578: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 579: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 612: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 613: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 646: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 647: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 680: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 681: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 714: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 715: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 748: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 749: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 782: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 783: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 816: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 817: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 850: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 851: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 884: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 885: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 918: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 919: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 952: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 953: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 986: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 987: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1021: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1039: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1057: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1075: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1093: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1111: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1129: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1147: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1165: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1183: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1201: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1219: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1237: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1255: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1273: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1291: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1309: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1327: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1345: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1363: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1381: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1399: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1417: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1435: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1453: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1471: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1489: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1507: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1525: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1543: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1560: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1561: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1595: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1596: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1643: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1644: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1677: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1678: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1712: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1713: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1760: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1761: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1795: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1813: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1831: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1867: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1885: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 7 plan 1903: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 0: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 1: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 2: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 3: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 4: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 5: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 6: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 7: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 8: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 9: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 10: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 11: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 12: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 13: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 14: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 15: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 16: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 17: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 18: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 19: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 20: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 21: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 22: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 23: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 24: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 25: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 26: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 27: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 28: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 29: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 30: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 31: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 32: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 33: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 34: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 35: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 36: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 37: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 38: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 39: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 40: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 41: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 42: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 43: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 44: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 45: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 46: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 47: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 48: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 49: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 50: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 51: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 52: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 53: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 54: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 55: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 56: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 57: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 58: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 59: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 60: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 61: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 62: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 63: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 80: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 124: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 260: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 396: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 792: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 1188: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 1584: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 1980: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 2376: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 2772: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 2896: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 3056: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 3100: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 3108: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 3212: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 3804: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 3948: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 5124: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 6300: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 7476: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 8652: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 9828: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 11004: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 12156: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 13308: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 14460: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 15612: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 16764: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 17916: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 21921: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 22272: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 22623: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 22974: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 23325: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 23676: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 24027: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 25731: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 26784: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 27837: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 28890: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 29943: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 30996: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 32049: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 35163: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 36216: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 37269: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 38322: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 39375: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 40428: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 41481: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 42534: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 42687: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 42840: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 43317: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 43470: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 43623: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 43776: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 43929: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 43930: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 44280: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 44631: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 44982: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 45333: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 45684: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 1: turn 8 plan 46035: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 0: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 24: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 48: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 72: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 96: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 120: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 144: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 168: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 192: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 216: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 240: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 264: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 288: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 312: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 336: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 360: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 390: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 396: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 420: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 426: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 1152: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 1176: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 1212: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 1218: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 1220: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 1222: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 1228: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 1234: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 1776: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 1800: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 1836: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 1842: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 1844: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 1846: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 1852: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 1858: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 2406: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 2412: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 2436: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 2442: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 2454: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 2460: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 2484: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 2490: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 2496: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 2529: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 2531: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 2533: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 2534: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 2552: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 2800: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 2836: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 2842: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 2866: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 2872: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 2884: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 2890: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 2914: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 2920: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 2983: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 2985: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 2987: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 2988: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 3344: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 3350: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 3352: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 3354: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 3360: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 3366: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 3380: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 3386: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 3388: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 3390: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 3396: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 3402: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 3444: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 3730: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 3736: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 3738: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 3740: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 3746: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 3752: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 3766: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 3772: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 3774: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 3776: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 3782: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 3788: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 3830: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 4227: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 4229: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 4231: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 4232: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 4243: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 4245: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 4247: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 4248: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 6 plan 4879: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 0: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 34: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 35: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 68: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 69: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 102: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 103: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 136: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 137: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 170: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 171: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 204: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 205: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 238: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 239: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 272: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 273: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 306: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 307: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 340: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 341: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 374: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 375: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 408: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 409: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 442: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 443: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 476: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 477: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 510: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 511: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 544: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 545: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 578: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 579: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 612: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 613: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 646: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 647: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 680: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 681: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 714: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 715: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 748: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 749: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 782: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 783: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 816: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 817: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 850: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 851: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 884: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 885: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 918: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 919: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 952: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 953: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 986: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 987: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1021: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1039: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1057: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1075: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1093: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1111: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1129: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1147: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1165: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1183: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1201: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1219: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1237: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1255: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1273: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1291: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1309: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1327: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1345: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1363: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1381: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1399: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1417: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1435: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1453: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1471: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1489: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1507: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1525: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1543: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1560: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1561: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1595: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1596: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1643: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1644: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1677: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1678: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1712: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1713: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1760: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1761: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1795: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1813: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1831: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1867: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1885: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 7 plan 1903: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 0: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 1: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 2: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 3: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 4: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 5: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 6: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 7: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 8: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 9: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 10: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 11: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 12: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 13: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 14: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 15: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 16: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 17: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 18: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 19: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 20: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 21: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 22: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 23: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 24: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 25: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 26: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 27: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 28: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 29: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 30: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 31: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 32: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 33: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 34: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 35: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 36: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 37: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 38: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 39: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 40: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 41: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 42: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 43: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 44: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 45: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 46: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 47: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 48: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 49: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 50: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 51: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 52: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 53: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 54: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 55: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 56: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 57: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 58: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 59: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 60: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 61: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 62: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 63: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 80: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 124: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 260: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 396: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 792: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 1188: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 1584: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 1980: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 2376: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 2772: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 2896: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 3056: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 3100: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 3108: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 3212: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 3804: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 3948: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 5124: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 6300: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 7476: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 8652: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 9828: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 11004: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 12156: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 13308: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 14460: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 15612: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 16764: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 17916: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 21921: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 22272: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 22623: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 22974: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 23325: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 23676: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 24027: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 25731: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 26784: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 27837: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 28890: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 29943: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 30996: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 32049: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 35163: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 36216: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 37269: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 38322: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 39375: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 40428: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 41481: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 42534: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 42687: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 42840: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 43317: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 43470: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 43623: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 43776: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 43929: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 43930: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 44280: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 44631: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 44982: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 45333: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 45684: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 2: turn 8 plan 46035: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 0: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 24: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 48: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 72: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 96: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 120: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 144: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 168: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 192: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 216: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 240: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 264: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 288: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 312: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 336: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 360: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 390: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 396: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 420: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 426: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 1152: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 1176: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 1212: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 1218: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 1220: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 1222: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 1228: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 1234: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 1776: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 1800: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 1836: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 1842: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 1844: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 1846: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 1852: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 1858: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 2406: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 2412: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 2436: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 2442: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 2454: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 2460: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 2484: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 2490: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 2496: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 2529: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 2531: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 2533: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 2534: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 2552: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 2800: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 2836: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 2842: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 2866: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 2872: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 2884: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 2890: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 2914: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 2920: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 2983: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 2985: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 2987: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 2988: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 3344: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 3350: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 3352: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 3354: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 3360: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 3366: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 3380: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 3386: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 3388: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 3390: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 3396: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 3402: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 3444: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 3730: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 3736: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 3738: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 3740: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 3746: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 3752: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 3766: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 3772: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 3774: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 3776: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 3782: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 3788: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 3830: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 4227: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 4229: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 4231: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 4232: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 4243: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 4245: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 4247: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 4248: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 6 plan 4879: casts 'Manabond' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 0: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 34: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 35: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 68: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 69: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 102: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 103: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 136: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 137: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 170: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 171: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 204: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 205: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 238: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 239: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 272: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 273: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 306: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 307: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 340: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 341: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 374: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 375: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 408: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 409: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 442: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 443: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 476: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 477: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 510: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 511: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 544: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 545: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 578: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 579: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 612: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 613: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 646: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 647: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 680: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 681: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 714: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 715: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 748: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 749: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 782: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 783: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 816: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 817: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 850: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 851: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 884: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 885: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 918: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 919: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 952: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 953: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 986: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 987: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1021: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1039: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1057: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1075: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1093: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1111: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1129: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1147: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1165: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1183: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1201: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1219: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1237: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1255: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1273: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1291: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1309: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1327: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1345: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1363: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1381: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1399: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1417: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1435: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1453: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1471: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1489: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1507: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1525: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1543: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1560: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1561: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1595: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1596: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1643: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1644: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1677: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1678: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1712: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1713: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1760: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1761: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1795: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1813: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1831: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1867: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1885: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 7 plan 1903: casts 'Sylvan Scrying' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 0: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 1: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 2: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 3: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 4: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 5: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 6: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 7: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 8: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 9: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 10: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 11: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 12: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 13: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 14: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 15: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 16: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 17: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 18: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 19: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 20: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 21: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 22: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 23: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 24: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 25: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 26: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 27: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 28: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 29: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 30: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 31: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 32: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 33: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 34: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 35: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 36: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 37: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 38: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 39: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 40: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 41: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 42: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 43: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 44: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 45: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 46: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 47: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 48: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 49: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 50: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 51: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 52: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 53: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 54: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 55: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 56: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 57: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 58: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 59: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 60: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 61: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 62: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 63: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 80: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 124: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 260: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 396: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 792: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 1188: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 1584: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 1980: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 2376: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 2772: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 2896: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 3056: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 3100: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 3108: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 3212: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 3804: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 3948: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 5124: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 6300: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 7476: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 8652: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 9828: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 11004: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 12156: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 13308: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 14460: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 15612: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 16764: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 17916: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 21921: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 22272: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 22623: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 22974: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 23325: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 23676: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 24027: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 25731: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 26784: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 27837: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 28890: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 29943: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 30996: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 32049: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 35163: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 36216: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 37269: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 38322: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 39375: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 40428: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 41481: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 42534: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 42687: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 42840: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 43317: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 43470: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 43623: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 43776: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 43929: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 43930: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 44280: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 44631: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 44982: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 45333: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 45684: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 7001 gi 3: turn 8 plan 46035: casts 'Archdruid's Charm' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 5 plan 0: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 5 plan 20: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 5 plan 40: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 5 plan 60: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 5 plan 80: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 5 plan 120: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 5 plan 141: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 5 plan 182: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 5 plan 223: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 5 plan 266: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 5 plan 351: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 0: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 1: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 2: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 3: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 4: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 5: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 6: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 7: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 8: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 9: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 10: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 11: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 12: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 13: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 14: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 15: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 16: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 17: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 18: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 19: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 20: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 21: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 22: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 23: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 24: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 25: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 26: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 27: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 28: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 29: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 30: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 31: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 32: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 33: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 34: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 35: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 36: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 37: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 38: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 39: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 40: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 41: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 42: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 43: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 44: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 45: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 46: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 47: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 48: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 49: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 50: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 51: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 52: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 53: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 54: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 55: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 56: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 57: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 58: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 59: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 60: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 61: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 62: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 63: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 64: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 65: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 66: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 67: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 68: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 69: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 70: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 71: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 72: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 73: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 74: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 75: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 76: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 77: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 78: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 79: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 80: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 81: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 82: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 83: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 84: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 85: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 86: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 87: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 88: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 89: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 90: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 91: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 92: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 93: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 94: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 95: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 96: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 97: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 98: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 99: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 100: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 101: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 102: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 103: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 104: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 105: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 106: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 107: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 108: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 109: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 110: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 111: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 112: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 113: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 114: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 115: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 116: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 117: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 118: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 119: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 120: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 121: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 122: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 123: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 124: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 125: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 126: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 127: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 128: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 129: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 130: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 131: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 132: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 133: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 134: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 135: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 136: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 137: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 138: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 139: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 140: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 141: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 142: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 143: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 144: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 145: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 146: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 147: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 148: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 149: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 150: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 151: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 152: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 153: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 154: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 155: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 175: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 210: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 245: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 280: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 315: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 389: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 409: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 429: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 449: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 469: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 529: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 549: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 569: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 589: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 0: turn 6 plan 609: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 5 plan 0: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 5 plan 20: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 5 plan 40: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 5 plan 60: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 5 plan 80: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 5 plan 120: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 5 plan 141: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 5 plan 182: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 5 plan 223: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 5 plan 266: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 5 plan 351: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 0: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 1: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 2: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 3: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 4: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 5: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 6: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 7: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 8: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 9: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 10: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 11: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 12: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 13: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 14: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 15: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 16: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 17: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 18: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 19: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 20: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 21: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 22: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 23: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 24: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 25: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 26: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 27: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 28: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 29: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 30: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 31: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 32: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 33: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 34: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 35: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 36: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 37: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 38: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 39: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 40: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 41: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 42: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 43: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 44: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 45: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 46: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 47: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 48: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 49: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 50: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 51: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 52: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 53: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 54: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 55: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 56: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 57: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 58: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 59: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 60: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 61: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 62: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 63: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 64: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 65: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 66: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 67: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 68: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 69: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 70: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 71: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 72: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 73: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 74: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 75: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 76: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 77: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 78: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 79: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 80: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 81: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 82: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 83: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 84: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 85: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 86: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 87: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 88: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 89: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 90: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 91: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 92: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 93: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 94: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 95: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 96: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 97: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 98: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 99: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 100: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 101: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 102: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 103: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 104: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 105: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 106: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 107: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 108: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 109: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 110: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 111: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 112: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 113: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 114: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 115: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 116: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 117: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 118: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 119: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 120: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 121: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 122: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 123: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 124: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 125: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 126: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 127: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 128: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 129: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 130: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 131: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 132: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 133: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 134: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 135: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 136: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 137: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 138: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 139: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 140: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 141: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 142: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 143: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 144: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 145: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 146: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 147: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 148: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 149: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 150: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 151: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 152: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 153: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 154: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 155: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 175: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 210: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 245: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 280: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 315: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 389: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 409: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 429: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 449: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 469: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 529: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 549: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 569: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 589: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 1: turn 6 plan 609: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 5 plan 0: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 5 plan 20: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 5 plan 40: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 5 plan 60: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 5 plan 80: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 5 plan 120: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 5 plan 141: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 5 plan 182: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 5 plan 223: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 5 plan 266: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 5 plan 351: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 0: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 1: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 2: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 3: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 4: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 5: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 6: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 7: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 8: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 9: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 10: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 11: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 12: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 13: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 14: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 15: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 16: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 17: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 18: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 19: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 20: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 21: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 22: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 23: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 24: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 25: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 26: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 27: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 28: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 29: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 30: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 31: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 32: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 33: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 34: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 35: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 36: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 37: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 38: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 39: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 40: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 41: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 42: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 43: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 44: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 45: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 46: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 47: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 48: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 49: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 50: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 51: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 52: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 53: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 54: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 55: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 56: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 57: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 58: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 59: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 60: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 61: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 62: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 63: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 64: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 65: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 66: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 67: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 68: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 69: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 70: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 71: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 72: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 73: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 74: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 75: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 76: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 77: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 78: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 79: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 80: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 81: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 82: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 83: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 84: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 85: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 86: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 87: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 88: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 89: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 90: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 91: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 92: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 93: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 94: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 95: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 96: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 97: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 98: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 99: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 100: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 101: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 102: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 103: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 104: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 105: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 106: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 107: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 108: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 109: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 110: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 111: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 112: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 113: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 114: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 115: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 116: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 117: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 118: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 119: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 120: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 121: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 122: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 123: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 124: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 125: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 126: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 127: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 128: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 129: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 130: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 131: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 132: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 133: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 134: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 135: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 136: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 137: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 138: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 139: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 140: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 141: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 142: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 143: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 144: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 145: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 146: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 147: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 148: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 149: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 150: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 151: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 152: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 153: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 154: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 155: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 175: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 210: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 245: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 280: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 315: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 389: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 409: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 429: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 449: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 469: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 529: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 549: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 569: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 589: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 2: turn 6 plan 609: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 5 plan 0: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 5 plan 20: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 5 plan 40: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 5 plan 60: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 5 plan 80: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 5 plan 120: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 5 plan 141: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 5 plan 182: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 5 plan 223: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 5 plan 266: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 5 plan 351: casts 'Exploration' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 0: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 1: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 2: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 3: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 4: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 5: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 6: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 7: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 8: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 9: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 10: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 11: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 12: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 13: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 14: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 15: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 16: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 17: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 18: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 19: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 20: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 21: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 22: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 23: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 24: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 25: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 26: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 27: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 28: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 29: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 30: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 31: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 32: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 33: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 34: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 35: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 36: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 37: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 38: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 39: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 40: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 41: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 42: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 43: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 44: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 45: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 46: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 47: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 48: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 49: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 50: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 51: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 52: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 53: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 54: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 55: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 56: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 57: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 58: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 59: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 60: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 61: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 62: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 63: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 64: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 65: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 66: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 67: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 68: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 69: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 70: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 71: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 72: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 73: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 74: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 75: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 76: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 77: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 78: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 79: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 80: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 81: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 82: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 83: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 84: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 85: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 86: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 87: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 88: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 89: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 90: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 91: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 92: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 93: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 94: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 95: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 96: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 97: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 98: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 99: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 100: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 101: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 102: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 103: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 104: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 105: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 106: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 107: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 108: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 109: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 110: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 111: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 112: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 113: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 114: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 115: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 116: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 117: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 118: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 119: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 120: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 121: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 122: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 123: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 124: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 125: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 126: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 127: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 128: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 129: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 130: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 131: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 132: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 133: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 134: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 135: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 136: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 137: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 138: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 139: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 140: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 141: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 142: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 143: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 144: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 145: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 146: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 147: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 148: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 149: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 150: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 151: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 152: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 153: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 154: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 155: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 175: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 210: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 245: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 280: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 315: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 389: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 409: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 429: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 449: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 469: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 529: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 549: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 569: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 589: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 8001 gi 3: turn 6 plan 609: casts 'Burgeoning' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- claude_sweep recorded at commit dab956a9d (HEAD c881e1f36d77); re-run if play changed since (play_invariants + smoke digests track play live).

<!-- verify_deck:end -->
