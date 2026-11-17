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
| I3 | Exploration (static drops + multi-drop search route), Burgeoning (opponent-turn crossing fan), Manabond (end-step axis) | IN PROGRESS (see I3 cards) |
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

## Approved deferrals

(none)

## Verification

(Stage 5 verdicts)
