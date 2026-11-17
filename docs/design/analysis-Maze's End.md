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
33. **Talon Gates' ETB "up to one target creature phases out" is NOT modelled** (inert collapse, disclosed in the
   bracket note; the engine has no phasing). Every choice reaches the same game in a goldfish: the list has no creature
   of ours and makes none, and the passive opponent's creatures never attack or block. Not offered as a viewer choice.
   PROVISIONAL (USER sign-off pending) -- re-check if the list gains a creature.

## Approved deferrals

(none)

## Verification

(Stage 5 verdicts)
