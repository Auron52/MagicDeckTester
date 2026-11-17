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
| I2 | Land-entry pipeline: controller-aware enters-tapped predicate, Gond Gate, Amulet of Vigor (single + multi-copy float), Thran Portal, Gateway Plaza, the five chosen-colour CLB Gates + the land-entry pin axis, The Black Gate | IN PROGRESS |
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
15. **PROVISIONAL (USER sign-off pending): an effect put that carries no pin takes the provider's choice.**
   Archdruid's Charm's tapped put of a CLB Gate / Thran Portal locks the provider's colour (`EtbChosenColor`:
   fewest sources among the colours the hand demands, else the admitted menu's first), and a Charm-fetched
   Gateway Plaza pays when it can. The Charm's find rides the resolution tutor axis (`Plan::tutor_choice`),
   which has no slot for the found land's entry choice, so inside the search window this is an UNSEARCHED pick
   (a no-greedy gap, small: one Charm in the list, and the colour only matters this turn under Amulet / Gond).
   Proposed fix: fan the tutor axis per (land, admitted pin). Maze's End's fetch and the land drop ARE searched.
   I3's put routes (Burgeoning, Manabond) must carry a pin of their own or inherit this gap.
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
