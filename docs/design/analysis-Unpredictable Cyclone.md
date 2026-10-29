# Analysis ledger — Unpredictable Cyclone

Deck: `decks/Unpredictable Cyclone/Unpredictable Cyclone.cod` (U/R, splash B, cycling-into-Eldrazi
combo): 4 Unpredictable Cyclone, 4 Emrakul, the Aeons Torn, 1 Ulamog, the Ceaseless Hunger,
1 Ulamog, the Infinite Gyre; cyclers 4 Street Wraith, 4 Curator of Mysteries; selection 4 Brainstorm,
4 Ponder, 4 Brainsurge, 1 Long-Term Plans; 4 Remand, 2 Toxic Deluge, 2 Seething Song; 25 lands
(4 Scalding Tarn, 5 Island, 3 Mountain, 2 Steam Vents, 1 Watery Grave, 1 Blood Crypt, 1 Cascade Bluffs,
3 Myriad Landscape, 1 Xander's Lounge). Sideboard (3 Architects of Will, 2 Long-Term Plans) is
unreachable (no wish) and not scanned.

The USER asked for the analysis on 2026-10-26 ("Let's implement Unpredictable Cyclone ... That is
analyze it").

Branch/worktree: `cyclone-analysis` @ `.claude/worktrees/cyclone` (cut from origin
`7933887b7`). The primary checkout's uncommitted artifact-queue files belong to another session and
are untouched.

**The combo.** With Unpredictable Cyclone on the battlefield, cycling another nonland card replaces
the cycling draw: exile from the top until a card sharing a card type with the cycled card, cast it
free, rest to the bottom in a random order. The cyclers (Street Wraith, Curator of Mysteries) are
creatures, so the exile walks to the next creature -- ideally Emrakul (cast trigger: extra turn) or
an Ulamog. Top-setting (Brainstorm, Brainsurge, Ponder, Long-Term Plans, Curator's cycle scry)
arranges the next creature. Seething Song pays Cyclone's {3}{R}{R} on turn 3.

## Standing decisions (pass to every subagent)
- Goldfish: the opponent never casts spells or blocks; opponent-facing clauses are inert, but
  self-binding clauses bind us. Model the full card; goldfish-inert deferrals are PROVISIONAL until
  the user signs off.
- No greedy / heuristic substitute inside the search window (user hard rule). Choices a card
  introduces are SEARCHED, provider-narrowed (restrict/order only), or collapsed by a stated sound
  dominance fold, and surfaced to the viewer.
- Cards are generic (cards.json is deck-agnostic).
- Cast order / range / main-split / mulligan / discard buckets are USER-OWNED: author and present,
  never adopt unilaterally.
- One heavy job (build or batch) at a time; total threads <= 24.
- Agents: no `cd` in compound commands, no `rm` / `rm -rf`, no `timeout`.
- **WIN-TURN METRIC WITH EXTRA TURNS (USER RULING 2026-10-26: *"Importantly, extra turns do not count
  toward the turn count."*):** the reported win turn counts ROUNDS, i.e. how many turns the opponent was given, so an extra turn does
  not advance it: `win turn = turns taken - extra turns taken`. Counting every turn we take would make
  Emrakul's extra turn worth nothing against a passive opponent, whose turns are no-ops. The search's
  objective (every place a win turn is compared) must use the same measure, or the search will not
  value the extra turn.

## Stage 1 — coverage (2026-10-26)
Missing (11): Unpredictable Cyclone, Emrakul the Aeons Torn, Ulamog the Ceaseless Hunger, Ulamog the
Infinite Gyre, Street Wraith, Curator of Mysteries, Brainsurge, Long-Term Plans, Toxic Deluge, Watery
Grave, Xander's Lounge. Present (11): Brainstorm, Ponder, Remand, Seething Song, Scalding Tarn, Island,
Mountain, Steam Vents, Cascade Bluffs, Myriad Landscape, Blood Crypt (re-verify against Scryfall in
Stage 2). Engine has NO extra-turn support (new Tier 3).

## Stage 2 — cards (in progress)
Research fan-out launched 2026-10-26 (four Opus agents: cyclone / eldrazi+extra-turn / cyclers+lands /
spells+existing+provider). Full drafts: `logs/cyclone/research/{cyclone,eldrazi,cyclers,spells}.md`
(gitignored; the decisions below are the durable record).

Research verdicts:
| card | tier | key design |
|---|---|---|
| Unpredictable Cyclone | 3 | replacement at the 3 cycle sites (executor PerformDig, human-play TurnSolver ~59618, rollout dig loop ~61330) via one shared walk helper; real random bottom order (keyed sort, lockstep); searched `freecast_choice` (take/decline; folds to take when declining cannot be better); re-solve after a walk. Hazards: PerformDig "dug" detection keys on the draw counter (R1); a free cast recorded under an open breakpoint replays as a hand cast (R2). |
| Street Wraith | 2 | `cycling_life_cost 2`; payable only at life > 2 (paying to 0 loses to SBA: sound prune); inert Landwalk/Swampwalk keyword tags |
| Curator of Mysteries | 3 | cycle-or-discard watcher -> scry 1, ONE trigger per event, resolves before the cycling draw; N scry-1s fold exactly to "bottom top j, j=0..N" (searched) |
| Xander's Lounge, Watery Grave | 1 | triome w/ cycling {3}; shock |
| Brainsurge | 1 | draw_spell draw 4 + put_back_from_hand 2 (no draw-3 assumption in put-back code) |
| Long-Term Plans | 2 | new `tutor_library_position 3` (NOT tutor_to_top: routes to anti and ~30 sites read it as next-draw); provider TutorSearchWidth 32. **Built in phase 4.** |
| Toxic Deluge | 1 | **`goldfish_inert` (USER 2026-10-27: *"I wouldn't cast Toxic Deluge, personally. That is fine to prune, just like the do-nothing-in-goldfish cards"*)**. The research draft's faithful pay-X-life / -X/-X design (and its one niche line: killing our own Emrakul/Gyre to reshuffle the graveyard) is NOT built; the card is a dead draw in hand, as the USER rules. USER's reason: *"You can argue that getting the shuffle and card back in the library is useful, but it's not worth losing your threat on board."* (Use this as the card's bracket-note WHY.) |
| Myriad Landscape | — | already fully implemented (sac-search searched) |
| Scalding Tarn | — | note STALE: PerformFetch does shuffle; rewrite note |
| Ponder | — | note STALE: order axis is searched; rewrite note |
| Brainstorm, Remand, Seething Song, basics, shocks, Bluffs | — | match Scryfall (oracle punctuation advisories only) |

**Structural gap (cyclers agent): WHICH card to cycle is not searched.** The search branches only on
dig yes/no (`Plan::dig_choice`); `SelectDigSource` returns ONE name (generic: first affordable in hand
order), and generic `ShouldConsiderDig` only cycles when nothing is castable. In this deck that is the
main decision -> a greedy pick in the search window. Decision: enumerate cycling as a searched plan
action, one per distinct cycler (copies fold). Built generically, so other cycling decks' play may move
and is measured as a play change.

**Eldrazi + extra turn (eldrazi agent).** Emrakul {15} (cant_be_countered, `cast_trigger_extra_turns 1`,
`annihilator 6`, graveyard-shuffle trigger), Ulamog CH {10} (`cast_trigger_exile_target_permanents 2`,
indestructible), Ulamog IG {11} (`cast_trigger_destroy_target_permanents 1`, annihilator 4, indestructible,
graveyard shuffle). Extra-turn design: `turn_number` STAYS THE ROUND (~950 reads stay correct), new
`turn_seq` / `pending_extra_turns` / `extra_turns_taken` / `in_extra_turn`; ~35 same-turn sites move to
`turn_seq` (found by a temporary rename); depth counts rounds (an extra turn costs no ply, so it is always
searched inside the window); lockstep in GameEngine::RunTurnFrom and SimulateEndAndStartNextTurn.
**The opponent HAS permanents in 8/10 games** (`PopulateOpponentSpawns`, every deck, vanilla tokens T1-3):
Ulamog targets are usually the opponent's, annihilator is NOT inert, and the 2026-10-05 Eldrazi
Conscription "no OpponentSpawn for this deck" premise is false. Cast-trigger targets: mandatory, our own
permanents legal (CR 115.1/601.2c/603.3d); searched whole-turn rank axis `cast_trigger_target_choice`
(Phylath `landfall_target_choice` shape), provider orders only.

**Provider:** today routes to AntiLifegain by accident (Scalding Tarn's `fetch_land_types`). Create
`CycloneProvider : DeckProvider` (empty derivation + TutorSearchWidth 32), signature from Cyclone's
replacement param OR (Emrakul extra-turn AND Curator scry), routed first.

### Phase 1 (foundations) -- implemented 2026-10-26/27
| card / item | tier | what landed |
|---|---|---|
| Watery Grave | 1 | basic_land, produces U/B, `etb_pay_life_to_untap 2` (Steam Vents shape). Viewer: bucket A `land_entry`. |
| Xander's Lounge | 1 | basic_land Island Swamp Mountain, produces U/B/R, `enters_tapped`, `cycling_cost "{3}"` (Zagoth Triome shape). Viewer: bucket A (land drop; cycle plan). |
| Brainsurge | 1 | draw_spell `draw 4` + `put_back_from_hand 2`; bracket note mirrors Brainstorm's (searched put-back, ONE put-back pinned per plan -- a second put-back spell takes the provider default, flagged as mattering more here; instant-timing PROVISIONAL fold incl. the after-cleanup hand-size edge). Viewer: bucket A `dig` x2. |
| Street Wraith | 2 | NEW `cycling_life_cost` (int; loader requires `cycling_cost`, `""` for life-only). Helpers `CyclingLifePayable` / `CyclingPayable` / `PayCyclingLife` (SpellEffects.h, beside EffectiveCyclingCost). Applied at P1/P2 executor PerformDig, P3 human-play DigDraw, P4 rollout dig loop, P5 `::SelectDigSource`, P6 human-play offer probe, FluctuatorProvider::SelectDigSource + its two "free cycle" predicates, LandPlay.cpp's mana-sink probe (an unpayable Wraith is no sink; a payable one notes {0}, the probe's conservative direction). Payable only while life > 2 (pay-to-0 loses at SBA before the draw: sound prune, also in human play). NOT touched (verified): DecisionProviders.cpp draw-source bound `act = cycling mana` (an over-credit, which that bound may do), TH discard roles, the land dedup signature, `TurnSolver` early-tapped land rank (lands only). Inert `Landwalk` + `Swampwalk` Keyword tags (Card.h; the mask is **uint64, 44 -> 46 of 64 used** -- the Eldrazi draft's "32-bit mask is full" and the audit_card_fields.py comments saying so are STALE since 2026-09-26). Viewer: cycling_cost main_phase plays (P6 now life-gated); `cycling_life_cost` added to audit_viewer_decisions.py MAINPHASE_PARAMS. |
| Scalding Tarn note | -- | rewritten: the shuffle IS modelled (PerformFetch -> ShuffleAfterSearch); residual disclosed: a fail-to-find returns before the shuffle. PerformFetch's header comment fixed too. |
| Ponder note | -- | rewritten to describe the searched order axis (PonderOrderAxis, m!+1 dispositions, one Ponder varied per plan, provider-only narrowing, human `reorder` decision). |
| Myriad Landscape note | -- | "(two Forests or two Mountains here)" -> "(e.g. two Forests, or two Mountains)". |
| CycloneProvider | -- | `CycloneProvider : DeckProvider` (empty derivation), `Name()` "Cyclone", inline `Certificate()` NotAssessed (free casts off the walk; extra turn vs round), `TutorSearchWidth()` 32 behind default-on `MTG_CYCLONE_TUTOR_WIDTH` (heurarm slot `CYCLONE_TUTOR_WIDTH`). Routed FIRST. Signature: `cycle_draw_exile_shared_type_freecast` OR (`cast_trigger_extra_turns > 0` AND `cycle_or_discard_trigger_scry > 0`). The three params are ADDED to CardParams + BuildParamsFromJson but read by NOTHING yet and carried by no card -- **the deck still routes to AntiLifegain** until phases 2/3 add the Cyclone / Emrakul / Curator entries with them. |

Tests: `test/unit/test_cyclone_phase1.cpp` (card data; life boundary; executor PerformDig and rollout dig loop pay/refuse in lockstep; routing on synthetic registered carriers incl. the AND term; tutor width + =0 hatch). `MtgTestSeam::PerformDig` added.

**TODO (phase 3) -- DONE in phase 3:** when the Cyclone/Emrakul/Curator entries land, the deck routes to `Cyclone`: update the pin
`{"Unpredictable Cyclone", "AntiLifegain"}` in `test/unit/test_pirates_provider.cpp` (~:121) and add the three
params to `scripts/audit_viewer_decisions.py`'s manifest (the self-guard fails on an unmapped param on a deck card).

### Phase 2 (extra turns + the three Eldrazi) -- implemented 2026-10-27
**Extra turns, engine-wide (Tier 3).** `turn_number` stays the ROUND (USER 2026-10-26). New GameState fields
`pending_extra_turns` / `extra_turns_taken` / `in_extra_turn` + the deck stamp `deck_has_extra_turn`.
`TurnSeq()` = `turn_number + extra_turns_taken` is a DERIVED accessor, not the draft's stored `turn_seq`
(deviation): every unit test, scenario and replay that sets `turn_number` by hand stays consistent, and
TurnSeq() == turn_number for every other deck by construction.
- Transition: ONE shared `GameState::AdvanceTurnCounter()` in `GameEngine::RunTurnFrom(NewTurn)` and
  `SimulateEndAndStartNextTurn`. With an extra turn next, the opponent's notional upkeep (Asylum Visitor)
  and draw are skipped (both worlds). Scheduled spawns do not arrive in an extra turn (both worlds).
  DayNightTurnBoundary is unchanged (one call; no daybound card in the list). DrawStep's on-the-play skip
  keys on `TurnSeq() == 1`. `PlayOutFrom` loops while `turn_number < max_turns || pending_extra_turns`.
  Backstop `TurnSeqCapped(max_turns)` (turns taken > 4 x max_turns -> no-win) in PlayOutFrom,
  SimulateToEndImpl and FSLineWin, value-gated on extra_turns_taken.
- Same-turn sites moved to TurnSeq() (about 70 lines): m1_hand_turn, m1start_hand_turn, m2_stage_turn,
  combat_hand_turn, top_stacked_turn (including "previous turn"), every staged-expiry write and read,
  suspend arrive_turn (+ the viewer's counter display), the pay ledger (PayLedger.h, PayRollback,
  NoteUntapEffect x9), the landfall pin, g_bp_site_turn (+ the CantripOrderScope site turns),
  g_condemn_root_turn (+ its guards), DecisionRootTurn (+ its two readers), t_ko_turn, the shuffle salts,
  the rollout / diagnostic reshuffle seeds, the T1 forced-land debug, three provider memo keys
  (FluctKeepPayable, the EDF BoardKey pair), and the viewer per-turn maps (firebreathe / jitte / tap-pref /
  storage-hold, Combat.cpp's attackers chooser). Method: an exhaustive grep of all 954 `turn_number`
  reads (no macro or token pasting touches the identifier), each classified ROUND or SAME-TURN. The
  compile-break rename was NOT run (it lists the same 954 sites; skipped to save a rebuild, Q18).
  Everything left on turn_number is a game clock (win turns, cutoffs, horizons, provider "turn >= 3"
  heuristics, logs, inf_life_turn, TT bounds), which IS the round under the USER ruling.
- **Depth counts ROUNDS (Q9 default).** `DepthAcrossTurn(after, depth)` gives the ply back after a
  transition INTO an extra turn, at all seven FSLineTail transition sites (and their eot_seen keys). It is
  applied AFTER the transition, not at the FSLineTail call sites as the draft said (deviation), because a
  main-2 Emrakul grants the turn after the caller has already decremented. SimulateToEndImpl gives the
  entry round +1 when a rollout is entered at an extra turn's start (SolveWithLookahead, label playouts).
  So every `win_turn <= turn + depth - 1` window check stays exact in rounds, and an extra turn inside
  the window is always searched.
- Keys: BuildSimKeyBase, dominance::Build and RouteSig fold pending / taken / in_extra, value-gated.
- Turn-edge prunes are OFF for `deck_has_extra_turn` (Q13 default): FslEdgeNoReach, the rollout kill-only
  turn, the winless certificate, the label edge-tail elide and go-off dominance.
- Logs and viewer: the decision JSON gets `extra_turn` (k) + `turn_seq`, and play events get `turn_seq`
  (g_play_extra_turns_taken), only once an extra turn has been taken (byte-identical otherwise).
  GameLogger phases carry `extraTurn` and a digest fold only when nonzero. An "Extra turn begins" event.
  index.html: "Turn N (extra k)" in the turn bar; `turnKey()` (turn_seq ?? turn) for commit-turn, the loop
  tail, the sacrifice burst and the firebreathe / jitte / storage-hold side-channels.
  test/viewer_protocol_check.py: `_tkey()` at every same-turn key and comparison (frame identity, free
  casts, replicate, the tap-pref / firebreathe / storage specs, force-attackers from events). server.js
  passes the maps through unchanged. No existing reference can hold an extra turn, so all replay
  identically. Known display-only gap: the staged_until / arrive_turn badges are in turns-taken units (no
  card in the list stages or suspends).

| card | tier | what landed |
|---|---|---|
| Emrakul, the Aeons Torn | 3 | `cant_be_countered`, `cast_trigger_extra_turns 1` (TriggerKind::ExtraTurn via PushCastTriggers for hand casts and every PushFreeCast; the rollout resolves it inline in apply_one after FireOnCastTriggers), Flying, `annihilator 6`, `put_into_graveyard_shuffles_graveyard`. Protection from colored spells PROVISIONAL inert (Q12; scryfall_divergences entry). Legend rule: the existing keep-oldest fold. Viewer: none (display only). |
| Ulamog, the Ceaseless Hunger | 3 | `cast_trigger_exile_target_permanents 2` (TriggerKind::CastExileTargets, targets chosen at PUSH per CR 603.3d; distinct, both sides, our own permanents legal and forced when needed; fewer than 2 legal -> removed; re-checked on resolution; exile ignores indestructible; a token ceases to exist). Indestructible. Attack trigger "exile the top twenty" PROVISIONAL inert (Q11). Viewer: bucket A `target`, two sequential picks. |
| Ulamog, the Infinite Gyre | 3 | 10/10 (Scryfall, verified). `cast_trigger_destroy_target_permanents 1` (an indestructible target is a legal no-op; a destroyed creature dies through OnCreatureDies), `annihilator 4`, graveyard shuffle, indestructible. Viewer: bucket A `target`. |

- **Annihilator** (`ApplyAnnihilator`, SpellEffects.h, both worlds right after battle cry): the defending
  player sacrifices N permanents, oldest first (their choice, so a fixed rule, Q16); atk_idx is remapped
  across the erases; FireOppCreatureDies per creature. Deck-gated (`deck_has_annihilator`).
- **Graveyard shuffle** (`FireGraveyardEntryTriggers` / `PerformGraveyardShuffle` / `GyShuffleBatch`): hooked
  in DiscardCardFromHand and OnCreatureDies; batched over the cleanup discard (both worlds), a legend-rule
  SBA batch and the cast-trigger destroy, so the shuffle takes every card that moved in the same event;
  shuffled with ShuffleAfterSearch (search-salted). NOT hooked (disclosed in the code): stack ->
  graveyard (a countered creature; Emrakul cannot be countered and a Remanded Gyre goes to hand) and
  library -> graveyard mills. Deck-gated (`deck_has_gy_shuffle`).
- **Searched target axis** `Plan::cast_trigger_target_choice` (int16, because a two-target option list on a
  wide board exceeds 127; it fits Plan's padding, so sizeof is unchanged). The Phylath landfall shape: a
  whole-turn rank policy (`ScriptedCastTriggerTarget`, keyed on TurnSeq) over the OPTIONS, which are
  multisets of N identity-folded classes (`CastTriggerTargetFoldKey` = LandfallTargetFoldKey + controller)
  in `DecisionProvider::CastTriggerTargetOrder`'s order. Base rule (ORDER only): opponent first; for a
  destroy, our indestructible permanents next; then tapped lands, untapped lands, creatures by power,
  others, power >= 10 last. Emitted by AppendSubdecisionAxes for plans that hard-cast a targeted
  cast-trigger card. Width = the exact option count with every permanent the plan adds counted as a new
  singleton class (an upper bound). NO cap: the landfall axis caps at 4, and a cap here would make options
  unfindable at any budget. Installed by ApplyPlanDirect and by AIEngine::TakeTurn (+ re-solve). Folded
  into PlanSignature, the axis-variant skip, plan equality, the bucket hashes and the PlanDom census.
  Deck-gated (`deck_has_cast_target_trigger`). **Phase 3 must extend the emission to plans whose Cyclone
  walk can free-cast an Ulamog.**
- GameState grew 1152 -> 1160 bytes (the Dominance.h static_assert is updated, with the classification).
- Audits: MODELED_ELSEWHERE_KEYWORDS += "annihilator"; audit_viewer_decisions.py manifest rows (target x2,
  INERT_PARAMS x3); a DECISIONS.md row; scenario harness key `expect_exile_counts`.
- Routing: Emrakul now carries `cast_trigger_extra_turns`, but CycloneProvider's AND term also needs
  Curator's `cycle_or_discard_trigger_scry` (phase 3), so the deck still routes to AntiLifegain. Expected.
- Tests: test/unit/test_cyclone_phase2.cpp; scenarios cyclone_emrakul_extra_turn_counts_rounds,
  cyclone_two_emrakul_two_extra_turns_same_round, cyclone_ulamog_exiles_own_permanents_when_forced.
- Results: unit 2056/2056 (15 new cases), scenarios 317/317 (3 new: win T4 in rounds after a T3 Emrakul
  with an extra-turn attack; two Emrakuls -> two extra turns -> win T3; a forced Ulamog exile of two own
  Islands), smoke 149 passed with play-changed=0 (searched and d0), viewer_protocol_check --strict 0
  play-drift / 0 enum-gap / 0 board-diverged (668 refs), cost audit rc 0 (494/494), field audit rc 0
  (snapshot refreshed with the three Eldrazi; the --update pass hit 429s on three cards already in the
  snapshot), viewer-decisions static audit clean.

### Phase 3 (the searched cycle, Unpredictable Cyclone, Curator, Remand) -- implemented 2026-10-27
**The searched cycle (item 1, generic mechanism, Tier 3).** WHICH card is cycled is now a plan ACTION: a
"cycle seed" (`Action::Kind::DigDraw`, `dig_sacrifice` false, by NAME -- `hand_index` -1, `def` set;
`IsCycleSeed` / `CycleSeedOf`, TurnSolver.h), one per DISTINCT cycler the provider admits
(`DecisionProvider::SearchedCycleSources`, restrict / order only; copies fold: every world cycles by name).
CollectActions emits them in the decision space only (`g_search_candidate_enum || g_condemn_root_turn < 0`,
the Remand precedent) and never in human play. All seeds share ONE odometer family (`PlanGroupKey`
-6000000: one cycle per plan section), and `kDupCopies` (BuildCopyCap / CopiesOfNameUsed) stops a seed and a
cast spending one copy twice. Each seed carries its two in-resolution choices as pins: `devour_count` =
Curator's disposition j, `ponder_keep` = Cyclone's free cast (kCycFc*: -1 default, 0 decline, 1 take,
2 take-and-Remand). Keyed in both plan signatures ("CYCLE#name#fc#j"; fast + slow, lockstep) -- ActionFoldSig
and Action::operator== already fold both fields.
- **Realised as a chain** (`cycle_chain_step`, ApplyPlanDirect): the plan's own seed runs at the dig loop's
  position (after the plan's casts and trailing activations); the seed is paid (life + mana, the copy must
  still be in hand), discarded (CycleDiscardScope), Curator's triggers resolve with the pin, OnCardCycled,
  then the draw OR the Cyclone replacement; then EVERY step opens the site-4 re-solve, whose continuation
  may carry the NEXT seed or end the chain (EMPTY). So every step of a cycle chain is a searched decision,
  with the standing breakpoint machinery (wave 0, deferred waves, BpBaseCanon's base-plan canon).
  A seed in ANY continuation runs right after that continuation's cast section (`apply_continuation_plan`'s
  hook), so "Brainstorm, then cycle the Street Wraith it drew" is reachable (scenario
  cyclone_brainstorm_draws_wraith_cycles_same_turn) -- except an unbranched-CANON continuation at the other
  breakpoint sites (the executor MIRRORS those, casts only: lockstep); at site 4 a canon continuation's seed
  does run (a site-4 continuation reaches the executor only as recorded breakpoint_casts, never mirrored),
  so a base plan's chain is not cut at its first cycle. Each step is recorded as a DigDraw in the current
  sink (pushed BEFORE a walk -- R2 -- with its REALISED pins; at top level in its own main-level segment
  `rec_bp_ord = main_bp_ord + 1`). `BpDigFanoutForPlan` marks a seeded plan (exact: its chain always
  reaches site 4).
- **Executor replay ordering fix (generic, in `replay_recorded`).** A recorded DigDraw now dispatches the
  board activations of the records BEFORE it first (the rollout cycles after them). Found by the
  MTG_FD_ORACLE probe: a Brainstorm continuation's Myriad Landscape search (a shuffle) and the plan's
  cycle sat in one catch-all list; the executor deferred the activation past the cycle, so its walk found a
  Street Wraith where the rollout's (post-shuffle) walk found the kill (s7322 gi22: proven T7, realised T9;
  fixed: T7). Smoke stayed byte-identical (no other deck records a dig after a continuation activation).
- **Executor**: the committed line replays the DigDraw records through `AIEngine::PerformDigFull` (the
  record's pins + `resolve_stack`). The uncommitted path (`UseSurplusLandAbilities(..., cycle_seed)`)
  realises the plan's seed in EITHER main; a cycle-searching provider then stops (the rest of its chain was
  a decision this path has no answer for) and requests the second pass.
- **Opt-in, not forced (deviation from the brief; Q20).** `CyclesAreSearched()` / `SearchedCycleSources`
  default to false / empty: every other deck keeps the legacy dig loop byte-identically. CycloneProvider
  opts in (every distinct payable cycler, ORDER: creatures, other nonlands, lands; cheaper first). For an
  opted-in provider the legacy loop runs only for the horizon leaf's plan (its PLAYOUT policy:
  CycloneProvider's ShouldConsiderDig = generic empty-of-gas OR a replacer out with a nonland cycler held;
  SelectDigSource = the cheapest payable creature cycler; DigContinueAfterResolve true) and DigOfferAt
  offers no twins. The generic "only when nothing is castable" gate was NOT removed for the other decks:
  it is the USER's own 2026-09-29 rule (docs/design/generic-dig-gate.md), a provider restriction.
- **R1 fixed**: `PerformDigFull` returns {Failed, DrewLand, DrewNonland, Replaced}; the reactive chain
  breaks only on Failed (a replaced, draw-less cycle no longer ends it).

| card | tier | what landed |
|---|---|---|
| Unpredictable Cyclone | 3 | `cycle_draw_exile_shared_type_freecast` (+ cycling {2}). Replacement of the cycling draw at the three sites (executor `PerformDigFull`, the human DigDraw pass, the rollout's seeded chain + legacy loop) through ONE set of SpellEffects.h helpers: `CycleDrawReplacer` (nonland cycled card; one instance applies, CR 616.1; deck stamp `deck_has_cycle_draw_replacement`), `WalkExileUntilSharedType` (definition card types; Battle/Kindred included, Land excluded), `CycleHitCastable` (no land, a legal target, a cast left), `ResolveCycleFreeCastChoice`, `BottomInRandomOrder` (a CRN keyed sort with ShuffleAfterSearch's seed/salt/g_shuffle_eval; >= 2 cards bump search_count), `EmitCycloneWalk` (history: "cast (free)" / "bottom (random order)"). The cast is world-specific: executor `EffectHandler::PushFreeCast` (cast triggers fire: Emrakul's extra turn, the Ulamogs' targets) then bottom then the Remand; rollout/human `cyclone_replaced_draw` (bottom, EnterHand(Reveal) at the hand FRONT so apply_one casts this copy, `cascade_free` + `suppress_cast_record` (R2: the cast is not recorded as a CastFromHand; its nested records nest under the DigDraw) + apply_one). Whiff = a random put-back, no draw. Viewer: bucket A `free_cast` (source "Unpredictable Cyclone", walked shown), the plan label "cycle X (Unpredictable Cyclone: exile until a creature, may cast it free)". |
| Curator of Mysteries | 3 | 4/4 Flying {2}{U}{U} (Scryfall; the phase brief's "2/2 {2}{U}" was wrong), cycling {U}, `cycle_or_discard_trigger_scry 1`. HOOK RESOLVED: `DiscardCardFromHand` fires the watcher for every NON-cycle discard (cleanup); the three cycle sites wrap their discard in `CycleDiscardScope` (the hook stands down) and fire it themselves via `ResolveDiscardScryTriggers` with the seed's pin, after the madness drain and BEFORE OnCardCycled / the draw / the walk -- so a cycle fires EXACTLY once (unit-pinned). N scry-1 triggers fold to "bottom the top j" (pinned; exact only when every watcher scries 1, checked). Unpinned (human, playout, cleanup) = ScryTop per trigger with any land-ETB scry pin set aside. Deck stamp `deck_has_discard_scry_watcher`. Viewer: bucket A `scry`. |

**Free cast (Q3-Q5 defaults taken).** Take / decline / take-and-Remand is SEARCHED per cycle (the seed's
pin, the realised value on the record). The take-dominance fold is the PROVIDER's
(`DecisionProvider::FreeCastTakeDominated`, base false = everything branched): CycloneProvider folds a
vanilla-template creature or another replacer UNLESS it collides with a legendary permanent we control or
its targeted cast trigger would have to take our own permanents (fewer opposing permanents than targets).
A decline variant is emitted only when the library holds a non-dominated card of a shared type; a decline
pin on a dominated hit takes. The walk re-solves (Q4: every seeded step re-solves). The bottom order is
random for Cyclone only (Q5: cascade untouched). The playout default (`TakeFreeCast`) takes unless an
Ulamog's trigger would be forced onto our own permanents.

**Remand rulings (item 4, USER Q6).** Remand on a Cyclone FREE CAST: the take-and-Remand pin (offered with
a Remand in hand that the provider admits for some reachable hit; realised only when the Remand is payable,
else plain take). Rollout: `CastRiders::free_remand` lets apply_one's remand rider ride a free walk cast,
paying ONLY the Remand (TapForCostDirect); executor: PushFreeCast -> bottom -> TapForCost the Remand ->
erase the hit from the stack if counterable -> the shared `CastAndResolveRemand`. A cycler hit goes back to
hand (cycle it again); Emrakul (cant_be_countered) resolves and the Remand is a cantrip. PRUNED on a free-cast
Ulamog by `DecisionProvider::AdmitRemandOnFreeCast` (generic hook, default admit all; CycloneProvider:
refuse a counterable spell with a TARGETED cast trigger -- properties, not names). HAND-CAST self-Remand:
`DecisionProvider::AdmitRemandRider` (default admit all) read in CollectActions' remand post-pass;
CycloneProvider admits only target MV <= `MTG_CYCLONE_REMAND_MAX_MV` (int, default 1, -1 = never). It is an
INT, so the per-job arm is `valuearm::Arm::cyclone_remand_max_mv` (manifest key "cyclone_remand_max_mv"),
not the boolean HeuristicArm the brief named (Q23). The Stage 5 A/B (1 vs -1) is the orchestrator's.

**Cast-trigger targets on a free cast.** AppendSubdecisionAxes' `cast_trigger_target_choice` axis is now
also emitted for a plan whose cycle seed can walk to a targeted cast-trigger card
(`CycleSeedCastTriggerTargets`: EXACT -- the predicted hit below the pinned scry's bottomed cards -- when
a replacer is out and the plan does nothing else; otherwise every library card of a shared type, an upper
bound). Still uncapped.

**Wiring.** Routing pin `{"Unpredictable Cyclone", "Cyclone"}` (test_pirates_provider.cpp) -- the deck
routes to CycloneProvider now. audit_viewer_decisions.py MANIFEST rows (`cycle_draw_exile_shared_type_freecast`
-> free_cast, `cycle_or_discard_trigger_scry` -> scry) and the oracle cross-check token. DECISIONS.md rows
(which-card-to-cycle `main_phase`, the Cyclone `free_cast` + its Remand prompt, the Curator `scry`).
main.cpp `SummarizePlan` label. Human play: which cycler = the standalone dig plans; take/decline + the
Remand prompt = `free_cast` (the Remand prompt is a SECOND free_cast decision whose candidate is the Remand
in hand, zone "hand" -- a reuse, Q22); scry j = one `scry` prompt per trigger. MTG_DIG_CENSUS gains `seed=`.
The Stage 5h live surface check (audit_viewer_decisions.py without --no-sweep) was NOT run in phase 3.

**Tests.** test/unit/test_cyclone_phase3.cpp (10 cases: card data; routing; the walk -- shared types, an
artifact creature stopping at an artifact, an artifact-land hit not cast, the whiff = random put-back with no
draw, a cycled land not replaced, Cyclone's own cycling with none out, a Cyclone cycled with one out walking to
the next enchantment; LOCKSTEP executor PerformDigFull vs the rollout's seeded chain, library ORDER included;
decline (non-dominated Ulamog) / dominated decline-takes / take-and-Remand in both worlds; the Remand
restrictions incl. the per-job ceiling; Curator once per cycle (chooser count), pinned j, a cycled Curator not
triggering itself, a cleanup discard triggering; the scry-before-walk core line in both worlds; seed
emission counts + no two seeds per plan + no seed beside a cast of the same copy; R1 -- the reactive chain
continues past a replaced cycle). Scenarios: cyclone_wraith_walks_to_emrakul_extra_turn (round-3 kill),
cyclone_curator_scry_bottoms_wraith_walk_reaches_emrakul (needs j = 1), cyclone_brainstorm_draws_wraith_cycles_same_turn
(needs a CONTINUATION seed). Phase-1 test updated (its legacy-loop case is now the horizon leaf's plan).

**Results.** Unit 2066/2066; scenarios 320/320; smoke 149 passed, play-changed 0 on every deck (searched and
d0: the mechanism is opt-in and only the Cyclone deck, not yet in the suite, opts in);
viewer_protocol_check --strict 0 play-drift / 0 enum-gap / 0 board-diverged (668 refs); cost audit rc 0
(496/496); field audit offline rc 0 (snapshot refreshed with Cyclone + Curator; the --update pass hit 429s on
five cards already in the snapshot). Lockstep probe MTG_FD_ORACLE (d3 b10): 1 diverge in 24 games before the
replay-order fix (s7322), 0 in 64 + 96 games after.

**COST (measured 2026-10-27, d3 b10, no profile -- the deck has none yet).** The searched cycle costs
**1.71x CPU** against the legacy greedy cycle on the same binary (32 paired games, seed 7800: 466 s vs 272 s
user, 14.6 vs 8.5 s/game; avg 5.625 vs 5.656 turns -- the quality delta is inside the noise at this size).
The deck is expensive with or without it: single-thread 16 games (seed 7600) 12.7 s/game against Hinata2's
0.93 s/game at the same settings (~13.6x; the 3x suite-entry rule would block it today), and ~3 GB resident even
on one worker (4.8-5.8 GB on eight; Hinata2: 113 MB). Not root-caused in phase 3 (Q26). Census (96 games):
in-window automatic digs 0, playout (leaf) cycles 1.30M, searched cycle steps 48.8M.

## Optimization plan (USER 2026-10-27) -- the next phase after phase 4, before suite entry
USER: *"We'll have to optimize of course. We should have heuristics for searches (long term plans and fetches) and
likely prunes for cantrips as well. Fetches are easy You can just always take shock lands, prioritizing steam vents.
Arguably the triome should be fetched if we don't need the mana."*
- PROFILE FIRST (Q26: 13.6x Hinata2 at d3/b10, ~3 GB resident per worker): attribute the cost to axes (fetch target,
  tutor target, cantrip put-back/order, which-cycler, freecast, scry-j, Ulamog targets) before cutting anything.
- **Fetch target (Scalding Tarn; USER heuristic, a provider restriction).** Shock lands first, Steam Vents ahead of the
  others. Black only pays Toxic Deluge (goldfish_inert), so in practice Watery Grave = an Island and Blood Crypt = a
  Mountain. USER 2026-10-27: *"Honestly we can probably rank the other cards based on need. You only ever need 2 red,
  so blue would be fine to take after all steam vents are out."* So after the Vents are gone, RANK BY NEED: while we
  control fewer than 2 red sources, a red one (Blood Crypt, then Mountain); otherwise blue (Watery Grave, then Island).
  The deck's red pips: Cyclone {3}{R}{R}, Seething Song {2}{R}. Count a red source the way the mana payer would (Steam
  Vents, Mountain, Blood Crypt, Xander's Lounge; Cascade Bluffs filters {U/R} into RR, so state how it counts).
  Top-ranked land is the one-option pick. USER clarification: *"The point is that the 2 steam vents themselves are
  sufficient red."* So the rule is effectively: Steam Vents while any is left in the library, then blue (Watery Grave,
  then Island). The red-need check is only the general form and matters only when a Vents never reached play (still
  in hand, or bottomed); count red sources on the battlefield plus red lands in hand. Xander's Lounge (enters tapped; also cycles for {3}) is
  admitted ALONGSIDE the shock -- "arguably ... if we don't need the mana" -- so the search decides between them
  rather than a predicate guessing whether the mana is needed. Basics only once no shock is left (which keeps the basics
  for Myriad Landscape too). One-option fetch prunes are the explicitly allowed provider kind; held-out A/B
  vs the searched baseline is still run and reported.
- **Myriad Landscape (USER heuristic, provider restriction).** USER 2026-10-27: *"Whereas Myriad Landscape should
  consider 2 mountains if we are short on red between board and hand."* Two Islands is the default; two Mountains is
  ADMITTED (searched against two Islands) only while red sources on the battlefield plus red lands in hand number
  fewer than 2. Otherwise two Islands is the one-option pick.
- **The red count (shared by the Tarn fallback and Myriad Landscape).** USER 2026-10-27: *"Note that seething song
  can count as Red 2."* Seething Song ({2}{R}: add {R}{R}{R}{R}{R}) in hand counts as a red source, but it needs one
  real red source to be cast, so it can only be the SECOND: short on red = red lands (battlefield + hand) == 0, or
  red lands == 1 and no Seething Song in hand.
  USER 2026-10-27: *"Also, a triome in hand counts differently because we cannot get red from it this turn."* An
  enters-tapped red land in hand (Xander's Lounge) does NOT count toward this turn's red; it counts once it is on the
  battlefield. (Implementer note, for the USER to confirm if it matters: the same "this turn" logic means at most one
  red land from hand can count, since only one land can be played per turn.)
- **Long-Term Plans target (USER heuristic, provider restriction).** USER: *"Long-Term plans is a good way to get a
  missing cyclone or to set up an Emrakul for getting with Cyclone."* So the candidates are Unpredictable Cyclone (only
  when we have none in hand or on the battlefield) and Emrakul (third from the top, where a Cyclone walk or the
  top-setting cantrips reach it) -- USER: *"If we have no other way to put an Emrakul on top."* Agent's reading, for
  the USER to confirm: "another way" = an Emrakul already in hand plus a put-back cantrip we can cast this turn
  (Brainstorm / Brainsurge); then LTP-for-Emrakul is pruned. When both targets survive, the search picks. When NEITHER
  survives (a Cyclone in hand AND another way to top an Emrakul), casting LTP is pruned this turn (held), rather than
  falling back to the width-32 set -- the agent's default, flagged. Agent's fallback, for
  the USER to confirm: an Ulamog when no Emrakul is left in the library (Ulamog CH before Gyre: {10} vs {11}).
  Measured against the width-32 searched baseline.
  USER 2026-10-27 (confirms the readings): *"We can always cast long-term plans later If we have brainstorm + Emrakul in
  hand."* So holding LTP when neither target qualifies is right. On OTHER targets: *"how much do we need to consider
  something else missing from hand? For example a street wraith might be worth it if we have no cyclers, have
  brainstorm or brainsurge to draw it or need a few turns before Cyclone and have nothing else missing. I would
  probably only apply the nothing missing + no cyclers and see how that goes to start. Maybe even consider Seething
  Song if we have a way to pull it off the top."* So:
  - Street Wraith (the cycler target) is ADMITTED when there is no cycler in hand AND nothing else is missing (a
    Cyclone in hand or on the battlefield, and the Emrakul target not admitted). Start with only this condition. The
    USER's other two triggers (a Brainstorm / Brainsurge to draw it; several turns before Cyclone) are NOT built yet;
    they are the next refinement if this one measures well.
  - Seething Song: proposed, behind its own lever (default OFF): admitted when we hold a way to draw it off the top
    this turn or next (Brainstorm / Brainsurge / Ponder). Measured and reported.
  - **CLAIRVOYANCE CAVEAT (USER):** *"Unfortunately this deck is very very susceptible to clairvoyance artifacts so in
    some cases allowing it to be cast might give better results just because of the shuffling that occurs."* LTP
    shuffles, and the search sees the post-shuffle library (the engine-wide known-library simplification), so a less
    restricted LTP can look better in an A/B purely by buying the search a "good shuffle". Every LTP-restriction A/B
    must therefore be READ for this: for each game the restriction makes slower, check whether the unrestricted line's
    gain came from cards revealed by the shuffle (a target that did nothing itself) rather than from the tutored card.
    Report that split alongside the raw delta. The same applies to any other shuffle (fetches, Myriad Landscape,
    Emrakul / Gyre graveyard shuffles).
- **Cantrip prunes (Brainstorm / Brainsurge put-back, Ponder order/shuffle).** Candidate SOUND folds first (identical
  cards, a put-back set equal under the deck's needs), then proposed lossy prunes for the USER (e.g. put-backs keep
  Emrakul on top when a Cyclone walk can reach it; dead cards such as Toxic Deluge go back first). Each is measured.
- **Put-back / top-of-library (USER 2026-10-27):** *"The idea is very similar to Niv. You put stuff on top that you don't
  need immediately or that you like to cyclone cycle into. So Emrakul and ulamog are both a great choice to put on top,
  second or third from the top."* Brainstorm / Brainsurge put-backs and Ponder's order favour (a) cards not needed this
  turn or next and (b) Cyclone walk targets (Emrakul, either Ulamog), with the Eldrazi as the first creature from the top so
  a creature-cycle walk hits it. Feeds the generic cantrip design (docs/design/cantrip-search-optimization.md) as a
  provider ORDER first; any single pick stays a separate lossy lever (Q33).
- **The deck's plan (USER 2026-10-27):** *"You need to make sure Cyclone can be played as early as possible. It is also a
  key card to always keep 1-of between hand and board. Beyond that you need enough mana to play it and 1-3 cycling cards
  (preferring Street Wraith for the free cycle). Even better if you can rig the top card or cards of the library to drop
  an early Emrakul with Brainstorm, Brainsurge or Ponder. (I forgot the list doesn't play Preordain, but that makes sense
  given how much worse it is at doing this.)"* Reading for put-backs / Ponder: keep exactly one Cyclone (hand + board;
  a second is surplus, only one replacement applies), the mana for {3}{R}{R} (lands / Seething Song), and 1-3 cyclers
  (Wraith over Curator); put back the surplus (extra Cyclones, cyclers beyond 3, lands beyond the Cyclone need, dead
  cards) and the Eldrazi, with Emrakul (or an Ulamog) as the first creature from the top. Also input for the mulligan
  stage (keep requirements) and the USER-owned discard buckets -- parked until then, not adopted.
- **Put-back order follows the GO-OFF TURN (USER 2026-10-27):** *"It's a bit tricky though because the put back order
  actually depends on when you can go off. I recommend calculating that heuristically."* A provider estimate k = our
  draw steps before we can have Cyclone out AND cycle (Cyclone in hand/board, the {3}{R}{R} mana incl. land drops and
  Seething Song, a cycler and its cost). Going off this turn: Eldrazi on top. After k draws: the top k cards are what
  we still need, the Eldrazi at k+1, and still the first creature from the top at go-off. A provider ORDER (the searched
  fan stays findable); measured paired. USER confirmation: *"if it is this turn you are cycling with cyclone you would
  want the Emrakul (or ulamog is no Emrakul) on top. Otherwise you may want it second or third from the top."* -- Emrakul
  first; an Ulamog only when no Emrakul is available to put back. Why not on top otherwise (USER): *"so that you don't
  draw it and can drop it instead"* -- an Eldrazi drawn is stuck in hand (Emrakul is {15}); it must sit below the draws.
  Junk ABOVE the Eldrazi even when going off this turn (USER): *"there is actually reason to put it under a useless
  non-creature card you have in hand even if you are cycling with cyclone this turn. For example a Toxic deluge or extra
  Cyclone can be freely replaced by putting it above the Emrakul and then Cyclone cycling."* A creature-cycle walk skips
  noncreature cards and bottoms them, so dead noncreature cards from hand go ABOVE the Eldrazi: they are thrown away for
  free and the walk still hits the Eldrazi. Order at go-off: [dead noncreatures] then the Eldrazi (the first creature).
  Several cycles this turn (USER): *"But if you can cycle twice this turn and have multiple of Emrakul or Ulamog you put
  both back and cycle both."* With c payable creature cycles this turn and e Eldrazi in hand, put back min(c, e,
  put-back slots) Eldrazi as the top creatures in the order they will be hit (Emrakul first), dead noncreatures above
  or between them; each walk bottoms what it passes, so the next walk reaches the next Eldrazi.
  USER: *"Since that is an automatic win."* -- two Eldrazi dropped in one turn is treated as the top-priority line.
  Hit order ANSWERED (USER): *"Yes, Emrakul goes first. Technically it doesn't matter as much when you have 2 Emrakul
  but even in that case the extra copies still give extra turns that equate to 15 damage each."* -- Emrakul before
  Ulamog; every Emrakul copy is worth putting back (each extra turn is another 15-power attack).
  2HG (USER): *"I guess in 2HG 2 Emrakul kills while Emrakul + Ulamog may not."* (30 team life: two Emrakuls attack for
  30 in the extra turn; Emrakul + Ulamog CH is 25.) So when Eldrazi outnumber the cycles, prefer Emrakul copies over
  Ulamogs. USER: *"But you can make that rule general if you want."* -- so it is ONE rule for every format: Emrakul copies
  before Ulamogs, always (no format check needed).
  PURPOSE (USER): *"the point of all of these rules is to come up with heuristics with which we can effectively prune a
  number of search options."* -- the rules above are meant as provider RESTRICTIONS (prunes), each measured, not only as
  an order.
  THE SHUFFLE LINE (USER): *"there is a separate line we need to consider. In some cases you do want to use Brainstorm or
  Brainsurge to throw away bad cards with a shuffle, such as from a fetchland. This is an important line we need to
  consider and might be the one case where we don't always throw back all of our Eldrazi."* So the default prune is
  "the Eldrazi always go back (per the go-off order)", and the EXCEPTION branch, admitted only when a shuffle is
  available after the cantrip this turn (an uncracked / playable Scalding Tarn, an activatable Myriad Landscape, a
  castable Long-Term Plans), is "put back the worst cards, then shuffle". In that branch the put-back ORDER is
  irrelevant (the shuffle follows): a sound fold to the put-back SET. Clairvoyance caveat applies (the search sees the
  post-shuffle library).
  When (USER): *"So that would be the 'have 1-2 cards that do nothing or practically nothing and are missing pieces we do
  need' line. We need to support playing the fetch lands after the brainstorm to do this. Other times you just throw away
  1 card by shuffling next turn."* So the branch is admitted when the hand holds 1-2 (near-)dead cards AND a needed piece
  is missing. Two shapes: (i) THIS TURN -- cantrip first, THEN the land drop is the fetch (or Myriad / LTP) and the shuffle
  takes both put-backs away; this needs the search to be able to play the land drop AFTER the cantrip (verify it can);
  (ii) NEXT TURN -- put a keepable card on top (it is drawn in the draw step) and the dead card second, then next turn's
  fetch shuffles the dead card away.
  Never when ready (USER): *"Naturally, you don't do this when you are in position to go off."* -- the shuffle branch is
  admitted only when the go-off estimate says we cannot go off this turn OR next turn (USER: *"If it says we can go off
  next I wouldn't consider it either, since we can still set up the Eldrazi on top for the next turn."*).
- **Ponder (USER 2026-10-27):** *"Are we treating which cards to keep on top with ponder the same as Brainstorm? That is
  what I recommend there. We can search the shuffle as needed and realistically it probably is safest to do so most of
  the time."* So Ponder's keep orders are scored by the SAME go-off scorer as the Brainstorm put-back (position terms
  only; Ponder removes nothing from hand). Ponder's shuffle is ALWAYS searched: it is never pruned, and it sits in the
  head of the order (the future wave 0) beside the best keep order. Design: `docs/design/cantrip-search-optimization.md`
  §3.2 (e227e9db7).
- Every heuristic lives in CycloneProvider (restrict/order only), each prune behind its own lever, measured paired on
  held-out seeds; results to the USER.

### Phase 4 (Long-Term Plans, Toxic Deluge; then the empty-fetch shuffle) -- implemented 2026-10-27
| card | tier | what landed |
|---|---|---|
| Long-Term Plans | 2 | {2}{U} instant (Scryfall). NEW int `tutor_library_position` (3); the loader refuses 1 (that is `tutor_to_top`) and any combination with `tutor_to_hand` / `tutor_to_top`. NOT `tutor_to_top` (about 30 provider sites read it as "the found card is the next draw", and it alone sets the anti signature). Shared predicate `IsLibraryTutor(p)` (CardDatabase.h, beside IsManaConversionSource). Resolution = the shared `PerformTutor` (both worlds): remove, search shuffle, insert at `min(N-1, library size)` (fewer than three cards left -> the bottom, the ruling); NOT a top stack (`top_stacked_turn` unset); the reveal disposition reads "3rd from top". Target SEARCHED (`Plan::tutor_choice` / named `tutor_target` variants over the generic every-legal-name list, CycloneProvider width 32). Human play: the instant re-asks at resolution (`HumanPlayDefersTutorTarget`); a -1 takes the default (Q27). Instant timing collapsed (USER-approved, Q7). Viewer: `main_phase` (tutor plan variants) + the `tutor` chooser; manifest row `tutor_library_position -> main_phase`, oracle "search" token. |
| Toxic Deluge | 1 | {2}{B} sorcery (Scryfall). `goldfish_inert` (USER ruling; the Memory Lapse / Distorting Wake shape: never cast, a dead card that can still be bottomed, discarded or put back). Bracket note quotes the USER's two sentences as the WHY. The pay-X-life / -X/-X design in the research draft is NOT built. Viewer: none (self-declared inert). |

**`tutor_to_top` / `tutor_to_hand` site classification** (re-grepped 2026-10-27; 206 reads). Moved to `IsLibraryTutor`:
EffectHandler.cpp spell resolution (executor -> PerformTutor); TurnSolver.cpp apply_one's tutor branch (rollout twin);
CollectActions' tutor emission; the two tutor-axis fans in AppendSubdecisionAxes (index mode and named mode);
`PutBackPredictionUnsafe` (LTP shuffles before a same-plan Brainstorm / Brainsurge draws); the "plan may hold a land
at the bounce" predicate and the `may_draw` width predicate (conservative direction); the default-off
MTG_DOUBT_MAIN2 classifier; SpellEffects.h `HumanPlayDefersTutorTarget`. Added beside them:
`ActionReadsLibraryTop` (LTP writes the top three after a shuffle -> a fetch crack before vs after it differs; no
`tutor_types` test, any card is legal). LEFT ALONE (they mean something LTP is not): `ParamKeyedDrawClass` (LTP gains
no hand card and opens no breakpoint; listing it would make the payer's unseen-colour guard treat it as an opener),
`PutBodyGainsCards`, the AcqResolve / TopResolve breakpoint masks and re-arms (TurnSolver ~58290 / ~70217, AIEngine
x4: next-draw semantics), SpellEffects.h ETB tutors (~10331, permanents only) and the Hoof projection (~18925), the
aura-swap diagnostic key (~1646), the equipment and Genesis Wave predicates, every provider-specific site in
DecisionProviders.cpp (cast ranks, discard buckets, finders, wish logic, routing -- LTP deliberately sets no
signature: a generic blue tutor), LandPlay.cpp's Worldly-Tutor line.

**Tests.** test/unit/test_cyclone_phase4.cpp (6 cases: card data + routing still Cyclone + LTP alone not AntiLifegain;
placement at library sizes 0/1/2/3/10 -> index 0/1/2/2/2, the shuffle counted, no top stack; LOCKSTEP executor stack
resolution vs the rollout's ApplyPlan, library ORDER; LTP-for-Emrakul then Brainstorm draws it; the cast is emitted,
Deluge never is, the candidate list spans every distinct name; human -1 takes the default). Scenario
cyclone_long_term_plans_emrakul_third_walk (Curator on top so a bare Wraith cycle walks to the Curator; LTP for
Emrakul first puts it third, the walk reaches it -> extra turn -> round-3 kill; negative control without LTP: no win).

**Phase 4b: the fail-to-find shuffle (Q14, a GENERIC play change).** One shared helper `WhiffShuffle`
(SpellEffects.h, beside ShuffleAfterSearch): a library search that finds nothing still shuffles (CR 701.19b).
Applied at every fetch-shaped early return, all shared by the executor and the rollout:
- `PerformFetch` (every fetchland): no legal target, and the drift guard (the chosen target gone). Life is still paid.
- `PerformLandTutorToBattlefield` (Crop Rotation): no land left, the drift guard, the non-land guard.
- `PerformTutorToBattlefield` (Dragonstorm, Natural Order, Chord of Calling, Green Sun's Zenith, Birthing Pod --
  cards with `tutor_shuffle_after`): an empty put list AND the explicit `kTutorDeclineTarget` fail-to-find variant
  (a chosen fail-to-find is still a search). Bilbo's caller keeps its once-per-activation guard (satisfied).
- `PerformTutor` (every card tutor): no legal candidate, and the drift guard -- only for a REQUIRED library search
  (`HumanFailToFindShuffles`: not a "you MAY search" -- the Harbingers / Forerunner decline instead -- not a wish,
  not a regrowth). The route tape (AccomplishedAlchemist) records such a whiff as a new sentinel
  `WhiffShuffledName` so its replay shuffles identically.
- Already correct, unchanged: `PerformLandSearch` (Evolving Wilds, Terramorphic, Myriad Landscape, Khalni, Primeval
  Titan, Rampant Growth: it shuffles on a whiff and returns early only on a declined optional search),
  `PerformEtbTutorToHandMulti` (Ranger of Eos), Bilbo, the human fail-to-find paths. Light-Paws' aura search is a
  "you may search": declining is a legal line, left as is.
- Levers: `MTG_LEGACY_WHIFF_NO_SHUFFLE=1` restores the old early return (an isolation arm, the MTG_LEGACY_SHUFFLE
  precedent); `MTG_WHIFF_TRACE=1` prints every REAL-resolution whiff to stderr. Viewer: a play event "searched the
  library, found nothing -- library shuffled".
- Notes: Scalding Tarn's residual sentence replaced; the six other fetchlands' stale "library order/thinning not
  modelled" sentences replaced; PerformFetch's header comment fixed.
- Unit: test_cyclone_phase4.cpp "Phase 4b" case (Tarn whiff + drift guard, Crop Rotation whiff, Worldly Tutor whiff
  shuffle, Giant Harbinger "may" whiff does NOT shuffle).

**Results (2026-10-27).** Commit (a): unit 2072/2072, scenarios 321/321, smoke 149 passed with play-changed 0 on
every deck (searched and d0), cost audit 498/498, field audit offline rc 0. Commit (b): unit 2073/2073, scenarios
321/321, viewer_protocol_check --strict 0 play-drift / 0 board-diverged / 0 enum-gap (668 refs), smoke 146 passed /
3 FAILED (GT NOT accepted -- the orchestrator's call), every move at **d0 only** (searched: 0 slower, 0 faster, 0
play-changed): antilife d0 7 games (gi944 7->loss, gi460 loss->8, gi482 7->6, four digest-only), fluctuator d0 9
(gi295 6->8, eight digest-only), pd d0 4 (gi299 loss->6, three digest-only); avg antilife 4.8920 -> 4.8920,
fluctuator 4.9870 -> 4.9890, pd 7.3010 -> 7.2980. **All 20 isolated to the whiff shuffle**: each single game
re-run as a one-game batch job (seed base+gi, game_index gi) reproduces GT under the (a) binary, reproduces the
smoke result under the (b) binary, and reproduces GT again under the (b) binary with MTG_LEGACY_WHIFF_NO_SHUFFLE=1;
MTG_WHIFF_TRACE=1 finds a REAL-resolution whiff in every one of the 20 games (antilife: a fetchland with no
fetchable land left, T5-T8; fluctuator: Enlightened Tutor with no artifact/enchantment left, T3-T7; pd: a Green
Sun's Zenith fail-to-find, T5-T6). Evidence: logs/cyclone/phase4b-whiff-evidence.txt (gitignored).

### Phase 5 (the cost gate: profile, sound folds, the USER's search heuristics) -- 2026-10-27
**COST ATTRIBUTION BEFORE ANY CHANGE (f586cb53b, written before the first edit).** Settings: the deck has NO profile, so
it plays at the built-in defaults -- the d3 rows pin d3 b10, the d5 rows get the built-in d5 at b20 -- and the DEFAULT
keep policy (min 1 / max 5 lands, curve check TwoDrop: mulligan a hand with fewer than two lands or no nonland of mana
value <= 2) with lookahead bottoming (no keep table: every mulligan's bottom is chosen by playing EACH candidate bottom
as a full game at play settings). Probe: 144 games pooled on 24 threads (d3 b10 x96 seed 72000, d5 b20 x48 seed 71000),
plus a single-thread perf + turn census (8 games, seed 7600) and the cantrip / seed / tutor attribution
(MTG_CANTRIP_ATTRIB, extended in this phase with the seed and tutor axes) over the 84 KEPT games.

| where the work goes | d3 b10 | d5 b20 |
|---|---|---|
| all games: units / game (thread-wall s / game, 24 threads) | 1.57M (29.1 s) | 2.77M (70.1 s) |
| games that mulligan (default keep policy) | 38% (37/96) | 48% (23/48) |
| share of ALL units spent in mulligan games (the bottoming trial games) | **83%** | **88%** |
| a mulligan game vs a kept game | 7.7x | 7.9x |
| kept games: units / game (Hinata2's suite rows) | 440K (42-49K) | 646K (55-63K) |

Kept-game search (84 games, 42.1M units; inclusive units of the plans carrying each variant, so the rows overlap):
| axis | units | share |
|---|---|---|
| Brainstorm / Brainsurge PUT-BACK variants (putback_choice >= 1; resolution width mean 55, median 57, p90 73) | 25.0M | **59%** |
| Ponder ORDER variants (m!+1 = 7) | 6.0M | 14% |
| plans carrying a cycle seed (the searched which-cycler chain) | 9.4M | 22% |
| seed PIN variants (Curator j > 0, Cyclone decline / take-and-Remand) | 1.1M | 2.7% |
| Long-Term Plans TARGET variants (width 32) | 1.9M | 4.5% (but 15% of all plans returned for scoring; one LTP fan reached 225K plans) |
| Scalding Tarn target | 0 (the generic default is ONE option: the library-order top pick) | -- |
| Myriad Landscape picks, Ulamog targets | not separable (rare) | -- |
| the extra-turn transition (Q13 turn-edge prunes off) | no lever to isolate it | -- |

Budget discipline (census, single thread): decisions overrun their budget (median 43K units against 9K; 13% of all units
are rolled-back overrun passes): the FIRST iterative-deepening pass over the root's candidate list does not fit, because
the list is wide (the put-back fan). Floor probe (MTG_PUTBACK_AXIS=0 MTG_PONDER_AXIS=0, both axes at the provider
default): units 4.55x (d3) / 3.88x (d5) fewer, kept games at Hinata's level (83K / 94K units), but worse play (avg win turn
5.44 -> 5.52 d3, 5.29 -> 5.67 d5, n = 48 / 24) and STILL 10.7 s / game at d5 because the mulligan games remain ~11x a
kept game.

**MEMORY (Q26's ~3 GB / worker).** Not a leak: the shared PLAN-CACHE pool (the enumeration memo's promoted plan lists +
the breakpoint continuation cache; ONE global pool sized from RAM, 2,400 MB here) fills to its cap. 24-thread probe:
hiwater 2,400 / 2,400 MB, 11,741 refused stores; with the pool raised to 11.7 GB (MTG_PLAN_CACHE_KB=12000000) it filled
that too within ten minutes (rss 19.5 G). One worker alone reaches 2.2 GB. The lists are what is huge: a put-back fan clones
every base plan casting a Brainstorm ~55 times, and the width-32 LTP axis clones every LTP plan once per name (one list
reached 225K plans). Hinata never fills the pool (113 MB). A refusal is result-neutral for a single thread but makes
per-game units -- and so budgeted play at the start gate's edge -- depend on what OTHER workers hold (the batch prints
the [determinism] WARNING): this deck's pooled results are not byte-reproducible across thread counts (Q34).

**WHAT WAS BUILT (phase 5).** Every lever is per-job (heurarm) so the arms pool in one batch; human play and MTG_UNPRUNED
keep every option (the engine-side bypasses of each hook).
- **THE CYCLE-SEED PIN FOLD** (`MTG_CYCLE_SEED_FOLD`, SOUND, default ON; TurnSolver.cpp `CycleSeedPinRedundant`, at the
  head of EnumeratePlans' eval_and_push). CollectActions cannot see the plan, so it emits Curator j = 0..(watchers out +
  in hand) and the free-cast pins whenever a Cyclone is out OR in hand. Inside one plan: a j above the watchers that can be
  out when the seed is cycled (out now + those the plan casts) bottoms exactly min(j, N) cards, and with no Cyclone out
  or cast by the plan the free-cast pin is never read -- both are the SAME state as a sibling the odometer emits beside them
  (same name, same other selections, same cost, the readable pin emitted first), so the duplicate is not emitted. Applied
  only where exact: a plan with any non-cast action, or a cast that draws / looks / tutors / Remands (a breakpoint whose
  continuation could cast a found Curator / Cyclone before the seed) is left alone. Pinned by
  test_cyclone_phase5.cpp (fold on vs off: only the unreadable pins vanish; a Curator-casting plan keeps j = 1; a
  Brainstorm plan is not folded). Q26 (a) (the EXACT walk of a seed-only plan with a Cyclone out) is NOT built: the seed
  pins are 2.7% of the work, so the remaining fold is small; recorded as owed.
- **SCALDING TARN** (`MTG_CYCLONE_FETCH_RULE`, the USER's rule, default ON; CycloneProvider::FetchCandidates /
  FetchSearchCandidates). Rank: an untapped-entering U+R land (Steam Vents) first; then by the shared red count -- short:
  red shock (Blood Crypt), red basic (Mountain), then blue; not short: blue shock (Watery Grave), blue basic (Island), then
  red; the enters-tapped triome last. The search's set: the top pick, plus the triome (Xander's Lounge) beside it.
- **THE SHARED RED COUNT** (CycloneShortOnRed): our permanents that make red (not a fetchland), Cascade Bluffs TWO beside
  another U/R land (none alone -- it then makes only {C}), at most ONE red land in hand that enters untapped (the Lounge in
  hand does not count), Seething Song in hand as the SECOND red only. Short = fewer than two.
- **MYRIAD LANDSCAPE** (`MTG_CYCLONE_MYRIAD_RULE`, the USER's rule, default ON; LandSearchPickOrder / Width / Key): two
  Islands first and the one option; two Mountains second and searched beside them only while short on red; a list with no
  two-Island pick keeps the full fan. The memo key carries the red verdict.
- **LONG-TERM PLANS** (`MTG_CYCLONE_LTP_RULE`, the USER's rule, default ON; TutorCandidates + CollectedActionAllowed):
  Unpredictable Cyclone while none is in hand or out; Emrakul unless an Emrakul AND a put-back cantrip are both in hand;
  Street Wraith (the cheapest creature cycler) when no creature cycler is in hand and nothing else is missing (a Cyclone
  held or out, no Eldrazi target admitted -- USER 2026-10-27, built as the first condition only); none admitted -> the
  cast is HELD (dropped from the enumeration; a resolution reached anyway, after a mid-plan change, keeps the full list,
  never a forced whiff). `MTG_CYCLONE_LTP_ULAMOG` (the agent's fallback, default OFF): an Ulamog once no Emrakul is left
  (Ceaseless Hunger first). `MTG_CYCLONE_LTP_SONG` (the USER's "maybe", default OFF): Seething Song while we hold a
  Brainstorm / Brainsurge / Ponder and none is in hand.
- **CANTRIP PROPOSALS** (the agent's, from the USER's examples; ALL default OFF, the USER's call): `MTG_CYCLONE_PUTBACK_RULE`
  (put-back ORDER: goldfish_inert first, then the Eldrazi -- Ulamogs before Emrakul so an Emrakul put back lands ON TOP --
  while a Cyclone is out or in hand, then lands beyond the one kept, then Remand, then the deck's shed order; order only);
  `MTG_CYCLONE_PUTBACK_SINGLE` (prune the put-back fan to that pick; the Niv BRAINSTORM_SINGLE shape; inert without the
  rule); `MTG_CYCLONE_PONDER_KEEPSHUFFLE` (Ponder: keep in the provider's order vs shuffle; the Niv shape).
- Instruments: MTG_CANTRIP_ATTRIB gains three rows (plans carrying a seed, a seed with a non-default pin, a tutor variant);
  MTG_AXIS_TRACE_DUMP=1 lists every plan of a traced list.

**MEASUREMENT (held-out, paired, ONE pooled batch, 2026-10-27; f3d4974fa).** 12 arms as per-job flags; d3 b10 x200 (seed
81000) for every arm, d5 b20 x100 (seed 82000) for four; 24 threads, MTG_PLAN_CACHE_KB=12000000 (the pool still filled:
results are paired but not byte-reproducible across interleavings, Q34). Reference = `all` (the shipped defaults: fold +
the USER's three rules ON, every proposal OFF). delta = arm - all in avg win turn (an unwon game scores 9); + = the arm
is SLOWER, i.e. the lever it turns off was helping. "slower / faster" count the games the ARM loses / wins.
| arm (what differs from `all`) | depth | n | delta avg turn | se | arm slower / faster | units vs all |
|---|---|---|---|---|---|---|
| base (fold + all three rules OFF = the committed tree before phase 5) | d3 | 200 | +0.120 | 0.047 | 38 / 19 | x1.075 |
| base | d5 | 100 | +0.110 | 0.078 | 15 / 11 | x1.108 |
| fold OFF | d3 | 200 | +0.030 | 0.023 | 5 / 3 | x1.037 |
| Tarn rule OFF | d3 | 200 | +0.060 | 0.041 | 30 / 18 | x0.948 |
| Myriad rule OFF | d3 | 200 | +0.010 | 0.012 | 4 / 2 | x1.007 |
| LTP rule OFF | d3 | 200 | +0.005 | 0.037 | 9 / 11 | x1.090 |
| + LTP Ulamog (OFF lever ON) | d3 | 200 | 0.000 | 0.000 | 0 / 0 (inert in this sample) | x1.000 |
| + LTP Seething Song (OFF lever ON) | d3 | 200 | 0.000 | 0.007 | 1 / 1 | x1.005 |
| + put-back RULE (order only) | d3 | 200 | +0.010 | 0.050 | 36 / 33 | x1.068 |
| + put-back rule + SINGLE (prune to the rule's pick) | d3 | 200 | **-0.105** | 0.058 | 35 / 44 | **x0.270** |
| + put-back rule + SINGLE | d5 | 100 | **-0.110** | 0.072 | 12 / 18 | **x0.246** |
| + Ponder keep-vs-shuffle | d3 | 200 | +0.010 | 0.030 | 13 / 11 | x0.978 |
| + all three cantrip proposals | d3 | 200 | -0.080 | 0.057 | 36 / 41 | x0.245 |
| + all three cantrip proposals | d5 | 100 | -0.080 | 0.077 | 19 / 21 | x0.227 |
Against `base`: the shipped set is -0.120 (d3, se 0.047) / -0.110 (d5, se 0.078) turns at x0.930 / x0.903 units; the
put-back single pick on top of it -0.225 (se 0.062) / -0.220 (se 0.099) at x0.252 / x0.222.
Readings: the fold is a sound collapse and stays ON; it is not byte-identical under a budget (fewer duplicates re-price the
first pass), as expected. The Tarn rule is the one USER rule with a clear gain. LTP is quality-neutral and 8% cheaper.
Myriad is neutral. The put-back SINGLE pick is BETTER at both depths and ~4x cheaper: the searched fan (width ~55) cannot
be covered by the budget, so the first pass overruns, rolls back and the search ends up with a worse line than the rule's
pick. Ponder keep/shuffle is neutral and buys little.

**THE CLAIRVOYANCE READING (USER 2026-10-27).** For each shuffle-bearing restriction, every game it made SLOWER (rule arm
worse than the lever-off arm) was replayed (a) single-threaded with the real-resolution tutor trace, and (b) under four
OTHER search-shuffle salts (shuffle_salt_search 1-4; same opening, different post-search shuffles), both arms:
- **LTP (11 slower games).** Trace split: 5 shuffle-only (the unrestricted search tutored a card it never used --
  Island, Seething Song, either Ulamog, an Emrakul a turn early), 2 the tutored card was used (Street Wraith on T3,
  Seething Song on T3), 3 the rule's arm cast LTP and the other did not (a different line, not a looser LTP), 1 no LTP
  difference. Salts: the rule is still slower in 1 of 11, faster in 2, tied in 8 (mean -0.14 for the rule). So the raw
  +0.005 is NOT a hidden cost of the restriction: its losses are shuffle luck.
- **Scalding Tarn (18 slower games).** Salts: still slower in 6, faster in 9, tied 3 (mean -0.04). Mostly luck; gi 25, 61,
  130, 154, 184 stay slower across salts and are the games to root-cause if the USER wants the rule refined.
- **Myriad Landscape (2).** 1 / 1.

**MEMORY, root-caused.** Single thread, 8 games (seed 7600), the plan-cache pool UNBOUNDED (MTG_PLAN_CACHE_KB=0): the
shipped tree's pool demand is 3.27 GB (max RSS 3.85 GB); the LTP rule does not move it (3.27 GB); the put-back SINGLE pick
takes it to 123 MB (max RSS 190 MB, Hinata's level). The ~3 GB / worker is the Brainstorm / Brainsurge put-back fan's plan
lists, held by the enumeration memo and the breakpoint continuation cache.

**COST AFTER (the shipped set: fold + the three USER rules; held-out seeds 81000 d3 / 82000 d5).**
| | before (`base` arm) | after (`all`, shipped) | + put-back SINGLE (proposal, OFF) |
|---|---|---|---|
| d3 b10 units / game (thread-wall s, 24 threads) | 1.39M (27.7 s) | 1.29M (26.0 s) | 0.35M (7.3 s) |
| d5 b20 units / game | 2.05M (41.4 s) | 1.85M (36.7 s) | 0.45M (9.4 s) |
| kept games d3 / d5 (units) | 428K / 637K | 411K / 574K | 98K / 118K |
| mulligan games d3 / d5 (units; 37% / 35% of games) | 3.03M / 4.67M | 2.80M / 4.22M | 0.78M / 1.08M |
| share of all units in mulligan games | 81% / 80% | 80% / 80% | 82% / 83% |
| plan-cache demand, one worker, 8 games | 3.27 GB | 3.27 GB | 123 MB |
Kept-game axes after (126 games, seed 81000 d3, the shipped set): put-back variants 57%, Ponder 13%, plans carrying a
seed 22%, seed pins 1.7%, LTP target variants 0.2% (from 4.5%). The put-back fan is still the search's cost and the
mulligan bottoming still ~80% of everything.

**THE COST GATE (`suite_gate.py --cost`, provisional cases at Niv's pre-keep-table shape -- smoke d3 30 / d5 15 / 2hg 30,
regression d3 2x40 / d5 2x20 / 2hg 40, overnight the ENTRY set; no backstop block, owed as for niv).**
| run | cyclone worst searched case | hinata worst (same run) | ratio | verdict |
|---|---|---|---|---|
| the shipped set (OFFICIAL) | 49,009 ms/game (d5 s3003) | 2,823 | **17.36x** | **FAIL** |
| what-if: + the three cantrip proposals (env) | 11,493 (d5 s2002) | 2,960 | 3.88x | FAIL |
| what-if: + proposals + MTG_BOTTOM_ROLLOUTS=0 (the blind bottomer: the cost a keep table would leave) | 2,003 (d5 s2002) | 2,809 | 0.71x | pass |
The what-ifs adopt nothing (env only; test/suite_cost.json restored to HEAD). Hinata's nine regression keys matched GT in
all three runs. The provisional cases are left UNCOMMITTED in test/regression_cases.sh (the brief: commit only on PASS).
The deck reaches the gate only with the USER's put-back decision (Q33) AND the mulligan stage's keep table (or cheaper
bottoming) -- Q32.

### Root-cause of the unrecoverable slower games (2026-10-27)
USER: *"root-cause every unrecoverable slower game and determine whether they are truly clairvoyance or if we are
actually making bad decisions."* The eight games whose rule-minus-off mean stayed > 0 over shuffle_salt_search 1-4
(fetch gi 25, 43, 61, 130, 154, 184; LTP gi 12; Myriad gi 184), replayed with the FROZEN f3d4974fa binary (`mtg.ab2`),
single-threaded, full logs. Scratch: `build/scratch/cyclone/rootcause/` (`runs/<arm><salt>_<gi>`, `bin/fmt.py`,
`bin/div.py`).
- **Reproduction.** All 15 salt-0 replays (7 R, 8 U) reproduce the ab2 batch's win turn exactly.
- **Why the search-salt ensemble could not tell luck from rules.** `shuffle_salt_search` changes only what the SEARCH
  sees after a shuffle; the REAL shuffles keep `shuffle_salt` = 0, so all four "salts" dealt the same post-fetch
  future. And the shuffle is the CRN `ShuffleByKey`: the order is a function of (seed, shuffle ordinal, each card's
  m_number), so two fetch targets give the SAME future minus a different card. A different target therefore
  changes the draws even when both searches play identically. Four search salts measure the search's knowledge, not
  the luck of the realised shuffle. The right ensemble varies the REAL salt: `MTG_SHUFFLE_SALT=k` (opening fixed), with
  `MTG_SHUFFLE_SALT_SEARCH=1000+k` (non-clairvoyant, k = 1-8) and `=k` (clairvoyant, k = 1-4). That is 12 real
  futures per game per arm.
- **Six of the eight diverge BEFORE the lever has made any decision in the real game.** Either no Tarn is in hand,
  or the first difference is the mulligan bottom. The lever reaches that decision only through the search's
  rollouts, or through the lookahead bottomer's trial games (one full CLAIRVOYANT game per candidate bottom, played
  under the lever). Those are butterflies.

| gi | lever | first divergence (state) | R (rule on) | U (rule off) | class | evidence |
|---|---|---|---|---|---|---|
| 25 | fetch | T1, the rule's own pick (hand Mountain, Ponder, Brainsurge, Curator, Tarn, Myriad; Tarn played) | Steam Vents, T2 Vents | Watery Grave, T2 Island (generic: first land in library order) | (a) | Mana is equal or better for R (Vents, Vents, Mountain). Draws are identical through T3; after that, R's library still holds the Grave and Island that U fetched. R's T4 Brainstorm draws Grave, Island, Myriad; U's draws Wraith, Island, Cyclone and wins T4. 12 real salts: -0.08 (3 slower / 3 faster). The slower salts diverge later at search choices (k5 T2 Wraith cycle; k7 T2 crack vs Myriad; k6 the search took the admitted Lounge on T1 holding Ponder) |
| 43 | fetch | T4, no Tarn in hand (Remand x2, Deluge x2, Vents, Island, Curator, Emrakul) | cycle Curator, Ponder, Remand the Ponder (draws Tarn) | land, pass | (c) | Play is identical in all 8 non-clairvoyant salts; 12 salts: 0.00 (1/2) |
| 61 | fetch | T1, no Tarn (Emrakul, Brainsurge, Curator, Lounge, Ponder x2, Island; Vents played) | no Ponder | Ponder | (c) | 12 salts: -0.17 (1/3) |
| 130 | fetch | T1, no Tarn (Curator, Ulamog CH, Song, Remand, Myriad, Grave, Cyclone) | NO land drop | Watery Grave | (c) | The R T1 is dominated (Myriad enters tapped for free). At d8 b0, R plays Grave T1 and wins T4 = U: RECOVERS. 12 salts: -0.33 (2/7) |
| 154 | fetch | T2 sequencing (Tarn, Ponder, Mountain, Island x2, Brainstorm, Brainsurge) | crack Tarn (Vents), then Ponder the shuffled library | play Island, Ponder the known top (Cyclone, Bluffs, Mountain), keep Tarn | (c) | Both lines were open to both arms; the rule only names the target. U's salt-0 gain rides the known base-order top. At d8 b0, R plays the SAME Tarn-first line and wins T3 (U at d3: T4): RECOVERS. 12 salts: +0.25 (3/3), within noise (se 0.35) |
| 184 | fetch | T1, no Tarn (Bluffs, Island, Remand, Mountain, Deluge, Wraith) | keep Wraith | cycle Wraith -> Island | (c) | At d8 b0, R wins T7 = U: RECOVERS. 12 salts: -0.08 (2/3) |
| 12 | LTP | mulligan bottom 2 of (Tarn, Curator, Deluge, Ponder, Wraith, Remand, LTP) | Street Wraith + LTP | Toxic Deluge + LTP | (c) bottomer | The pick follows the realised future: R makes 5 different picks over 9 futures. Both arms usually KEEP the goldfish-inert Toxic Deluge and bottom Street Wraith (Deluge kept in 7/13 R runs, 9/13 U runs). 12 salts: -0.50 (1/4) |
| 184 | Myriad | mulligan bottom 1 (Island, Bluffs, Remand, Myriad, Mountain, Deluge, Wraith) | Myriad Landscape | Remand | (c) bottomer | Bottom and play are identical in all 8 non-clairvoyant salts. At d8 b0, R wins T7 = U: RECOVERS. 12 salts: -0.17 (0/1) |

**Verdict: no (b), so no rule amendment.** No game shows the Tarn, Myriad or LTP rule removing a choice that a
non-clairvoyant player would make. Pooled over the 12 real futures, the six fetch games average **-0.069 (se 0.107;
12 slower / 21 faster)**: the rule is not slower on the very games picked for being slower. LTP gi 12 averages -0.50,
Myriad gi 184 -0.17. The one game decided by the rule's own pick (gi 25) is draw luck under the CRN shuffle.
Observations for the USER, not evidence against the rule:
- The rule takes Steam Vents even once red is met (gi 25 T2: Vents in play and Mountain in hand). That costs 2 life
  against a basic Island, and life never bound in these games.
- The search sometimes takes the admitted Xander's Lounge on T1 while holding a castable Ponder (gi 25 k6).
- d8 b0 for gi 25, 43, 61 and 12 was still running when this was written: `runs/d8R_<gi>`.

**Defects found (reported, not fixed):**
1. **A dominated T1 skip of the land drop** at d3 b10. Repro: `mtg.ab2 "decks/Unpredictable Cyclone/Unpredictable
   Cyclone.cod" --games 1 --seed 81130 --game-index 130 --depth 3 --budget-ms 10 --threads 1 --ignore-play-profile`
   (T1 plays no land while holding Watery Grave and Myriad Landscape). It recovers at `--depth 8 --budget-ms 0`. It
   looks like a budget-tie artifact; it is not the rule.
2. **The lookahead bottomer keeps the inert Toxic Deluge over Street Wraith / Curator** in most futures (gi 12;
   also gi 184 never bottoms Deluge). Each candidate is judged by ONE clairvoyant trial game, so the pick follows
   the library rather than the hand. It is parked with the mulligan stage (Q32). A candidate provider order for that
   stage: goldfish_inert cards first. That would be a lossy prune and needs measurement first.
3. **The game log omits Brainstorm put-backs.** A card drawn and then put back gets no DRAW entry, and there is no
   put-back action (e.g. `runs/Ufk1_154` T1: a 3-card Brainstorm logs 2 DRAWs, and the Cyclone it put back appears
   as the T2 draw). This is cosmetic for play, but it misleads anyone reading a log as the full record.

## Stages 3-5 (coverage re-check, baseline profile, cost gate, verification) -- 2026-10-27
Run on the SHIPPED configuration (phase-5 levers at their committed defaults: the cycle-seed fold and the USER's Tarn /
Myriad / LTP rules ON, every cantrip proposal OFF). Where marked, a second arm `single` = MTG_CYCLONE_PUTBACK_RULE=1 +
MTG_CYCLONE_PUTBACK_SINGLE=1 (the Q33 adoption as recommended; SINGLE alone is inert without the rule) was run beside it,
per-job, adopting nothing. Scratch: build/scratch/cyclone/s5 (build volume).

**Stage 3.** `analyze_deck.py --coverage-only`: 22/22 cards `full`, 0 missing; the sideboard (Architects of Will, 2 LTP) is
unreachable (no wish). Every bracket note is a description of modelled behaviour or a Tier-4 deferral already on the
ledger (Q7 instant timing, Q11 Ulamog CH attack exile, Q12 Emrakul protection, Q15 swampwalk, Q21 cleanup-discard scry,
Toxic Deluge goldfish_inert, Remand's self-target gaps): no Tier 1-3 gap. One STALE note fixed: Remand's listed "Remand on a
free cast" as unmodelled, but the Cyclone free-cast Remand is built (phase 3); the note now says so (other free casts stay
a gap). Cosmetic: the coverage parser truncates Myriad Landscape's note at the nested `[C]` (the card is `full`). Cost audit
498/498 match Scryfall; field audit (offline) rc 0 (oracle-text divergence advisories only: the bracket notes).

**Stage 4 (d402f8bcb).** `analyze_deck.py --no-rebuild` wrote the BASELINE profile: card scores (1000 games d5 b20), NO
hand-score gate (-1e18), the default keep block (min 1 / max 5 lands, curve_check two_drop, stop_at 4, bottom_order
count_first, no required pieces), no value_play (play settings = the built-in **d5 b20**, `source=default`), no keep table.
mulligan_flags: none. Cost diagnostic: HIGH_DISCOUNT_COMBO (keep the accurate aggregate; no reframe). **The mulligan rate
does not move (Q32 c):** the profile's keep block IS the built-in default, and with no hand-score gate the card scores are
not read by the keep; on the phase-5 probe seeds the profile's first-hand keep decisions are IDENTICAL to the default's,
game for game (37/96 = 38.5% at seed 72000, 23/48 = 47.9% at 71000; 33/96 = 34.4% at 81000). The analyzer's last stage, the
discard analysis (report only: a 400-game single-thread d3 labeller, ~5 h on this deck, then a rule A/B), was still running
at hand-off; it writes nothing to the profile (its report lands in the analyzer's JSON and logs/discard_analysis/); the
discard buckets are USER-owned.

**Stage 4a.** `provider_audit.py --check` rc 0: `Unpredictable Cyclone -> Cyclone`, certificate NotAssessed.

**Cost gate re-run with the profile attached (provisional cases applied from logs/cyclone/provisional-suite-cases.diff,
then restored to HEAD; MTG_PGO=0 both runs).**
| run | cyclone worst searched case | hinata worst (same run) | ratio | verdict |
|---|---|---|---|---|
| shipped set (OFFICIAL) | 65,216 ms/game (cyclone2hg d3 s2002; d5 s3003 62,011) | 6,220 (d5 s3003) | **10.49x** | **FAIL** |
| what-if: + put-back RULE + SINGLE (env only, nothing adopted) | 25,992 (d5 s2002) | 6,611 | **3.93x** | FAIL |
| (phase 5, for reference: shipped / + the three cantrip proposals) | 49,009 / 11,493 | 2,823 / 2,960 | 17.36x / 3.88x | FAIL / FAIL |
BOX-LOAD CAVEAT: Hinata's units are byte-identical to the phase-5 gate run, but its ms were 2.2-2.3x higher in both runs
here (host loadavg ~41-45 from ANOTHER container while this worktree ran ~24 threads), while the cyclone rows inflated
only 1.3-2.1x (the same units within 1%), so the shipped ratio moved 17.36x -> 10.49x for reasons that are not the deck.
The verdict does not depend on it: the shipped set fails by 3.5-5.8x the limit, and even the put-back single pick (Q33)
alone stays over 3x (3.93x; phase 5's three-proposal what-if 3.88x). The bottoming trials (Q32) are the rest. Restored
test/regression_cases.sh and test/suite_cost.json to HEAD; the cases stay uncommitted.

**Stage 5a: nonconv / fd-diverge at play settings (d5 b20), plus d3 b10.** Pooled batches (per-job arms; nonconv and the
fd oracle in separate processes, as verify_deck does).
| batch | binary | cells (games) | [nonconv] | [fd-diverge] |
|---|---|---|---|---|
| A (seeds 90000-92000) | 9341d9823 | d0 x1000; d3 b10 ship/single x150; d5 b20 ship/single x150 | 0 | -- |
| B (seed 93000) | 9341d9823 | d5 b20 ship/single x150 | -- | **1** (ship, s93143 gi143) |
| C (seed 94000) | 56f21c37a | d0 x150; d3 b10 ship/single x150; d5 b20 ship/single x150 | 0 | -- |
| D (seed 93000 again) | 56f21c37a | d5 b20 ship/single x150 | -- | 0 |
**The one hit was a LOCKSTEP DEFECT, fixed (56f21c37a).** s93143 gi143, proven T7, realised unwon: turn 7, Seething Song
paid by Steam Vents + Mountain + Island left Cascade Bluffs and RRRRR floating; the committed line cycled Curator of
Mysteries ({U}) -- payable only through the filter fed by a floating {R} -- drew the Street Wraith, cast Cyclone, cycled the
Wraith into Emrakul. The rollout's cycle seed pays through TapForCostDirect; the executor's PerformDigFull refused first on
`AvailableManaPool().CanPay`, which books a filter only as its own taps. The probe is deleted (the payer leaves the state
unchanged on failure; P3/P4 never had it). Pinned by test_cyclone_stage5.cpp; the game now realises T7; batch D (same seeds)
has 0 hits and only gi143 moved (units byte-identical). Unit 2079/2079, scenarios 321/321, smoke 149/149 PASS with
play-changed 0 on every deck.

**Stage 5b: multi-depth sanity (batch C, paired on seed 94000, n=150 each).**
| arm | d0 | d3 b10 | d5 b20 (play) | unwon d3/d5 | d5 vs d3 per game (slower / faster) |
|---|---|---|---|---|---|
| shipped | 8.700 (123 unwon) | 5.313 | **5.220** | 0 / 0 | 5 / 12 |
| single | -- | 5.113 | 5.140 | 0 / 0 | 10 / 6 |
Shipped is monotone in aggregate. Win turns are plausible for the deck (T5-T6 modal; T3-T4 with an early Cyclone; 2-3 % at
T8). The single arm's d5 is 0.027 worse than its d3 (10 slower / 6 faster). All 15 depth inversions (5 shipped, 10 single)
were re-run at d3 and d5 under four REAL futures (batch F2: `shuffle_salt` = `shuffle_salt_search` = 1-4, the clairvoyant
form of the real-salt ensemble; the opening is unchanged): shipped 1 slower / 0 faster / 19 tied, single 2 slower / 2 faster / 36 tied. In every
other future the two depths play to the same turn; the inversion belongs to the one realised future, i.e. which shuffle
the line happened to ride, not to a deeper search choosing worse. (A first ensemble varied `shuffle_salt_search` alone --
the search's knowledge, not the future: shipped 1/1/18, single 4/4/32, the same reading.) Budget escalation did not
recover ship gi24 (T6 at b20/b100/b400/b2000 vs d3's T5); it ties d3 in all four real futures. The d0 rollout policy is very
weak (77-82 % unwon at d0, every searched game won): 5i (the rollout-quality digest) is the indicated tool if leaf quality
is worked on.

**Stage 5c: budget starvation.** The slowest shipped games (T8 / unwon) of batch A re-run at 1x, 5x and 20x budget (d5 b20/b100/b400; d3 b10/b100/b400):
d5 (seed 92000): gi3 8->7->7, gi80 8->8->7, gi103 8->7->7, gi30 8 at all three; d3 (91000): gi0 8->7 (b100), gi1 8 at all.
E2 (seed 94000, d5): ship gi132 8->7->7, single gi14 8->6->6, single gi24 7->6->6, single gi134 8 at all. So b20 / b10 starve
a minority of the slow games by about one turn (the phase-5 reading: the put-back fan does not fit the first pass). The
UNWON games are not budget: d5 gi125 (unwon at b20/b100/b400: a mulligan to 5 that never sees a Cyclone, four Remands in
hand), d5 gi50 (mulligan to 5 keeping one Mountain, no blue source until T4), d3 gi53 (two Mountains, a hand of blue
cantrips; units identical at every budget), d3 gi148 (two Myriad Landscapes and no other land: neither can ever pay its own
{2}). All four are keep-policy / mana-screw games (the keep is USER-owned, Q32 d).

**Stage 5c2: horizon-honest tie-break** (`leaf_tiebreak_check.py --blocks 4 --games 60`, play settings, 480 games / 240
paired, on 16 cores): net -3 turns (9 worse / 11 better), binding 8.3 % (20 changed), halves +6 / -9 (they DISAGREE).
Script verdict KEEP THE DEFAULT (ON). The sample is small (the script's default is 12 x 1000 per arm, 50x this, ~12 h of the
box at this deck's cost); the halves disagreeing means the sign is not established. Default kept; no provider override.

**Stage 5h: the LIVE viewer sweep** (`audit_viewer_decisions.py`, 40 games from seed 9001, max 12 turns): expected
{dig, free_cast, land_entry, reorder, scry, target, tutor_etb}, observed all of them (+ discard); no HARD MISS, no
SELF-GUARD or DRIVER failure; oracle cross-check clean. **5h PASS.**

**Stage 5j: verify_deck.py** (after 1b18e71e8):
| gate | result |
|---|---|
| coverage | PASS (22/22 full) |
| card_costs | PASS |
| card_fields | PASS (644 cards; 17 allowlisted divergences) |
| clause_ledger | SKIP (covered by coverage + bracket notes + oracle diff) |
| regression_tiers | **FAIL** (expected: not in any tier -- the cost gate, Q32/Q33) |
| viewer | PASS |
| viewer_wiring | PASS (7 types: dig, free_cast, land_entry, reorder, scry, target, tutor_etb) |
| mismatch | PASS (seeds 7001/8001 x 60, d3 b20 nonconv + d5 b20 fd oracle) |
| play_invariants | PASS (8 games / 340 decisions) -- FAILED on the first run, fixed (below) |
| claude_sweep | SKIP (expected: 5d comes after Q33) |
| suite | **FAIL** (expected: same cause as regression_tiers) |
The first run's play_invariants FAIL (seed 8001 gi 0-3, "did not reach a result within 400 decisions") was the DRIVER: the
viewer always re-prompts, the menu offered a PHANTOM plan (turn 1, one Mountain: "cast: Seething Song", which the apply then
drops -- TurnSolver's order-free ritual affordability credit, measured load-bearing for Dragonstorm's go-off search), and the driver
re-picked it until the cap. Fixed in the tool (1b18e71e8): on the same menu coming back with `dropped_casts`, the driver
passes and records an advisory (24 advisories on seed 8001; the phantom stays visible). The menu entry itself is Q37. Two
tool notes: play_invariants seeds every game-index of a seed with the SAME `--seed` (claude-play keys the shuffle on the
seed), so its "4 games per seed" are one game four times (Q38); and the reference-reproducibility check run after the
executor fix: 668 refs, 0 play-drift / 0 board-diverged / 0 enum-gap.

### Cantrip item 1 (2026-10-27): the go-off estimate {k, c}, the scored put-back / Ponder ORDER, PUTBACK_RULE rebuilt
Design: `docs/design/cantrip-search-optimization.md` §3.2-3.3, §6 item 1; the USER's put-back / go-off / shuffle-line /
Ponder guidance above. Commits 496043cf0 (code), 966daefa3 (tests + scenarios), af6539dff (lockstep: Myriad read by
count), on top of 7865fead2.

**What was built.**
- **The generic hook** (DecisionProvider.h `CantripReadiness`, `TopDispositionView`, `CantripReadinessOf`,
  `ScoreTopDisposition`; defaults false, so every other deck is byte-identical). When the provider returns a readiness
  for a resolution, the engine scores every candidate and STABLE-sorts by score with the existing order as the tie-break,
  inside the shared functions: `PutBackPickAt` and its reference twin `PutBackCandidates` (Brainstorm / Brainsurge),
  `ScoredReorderCandidates` behind `TopDispositionCandidates` (Ponder), and `HeuristicTopDisposition` (the unpinned
  reorder default = the scored rank 0, so the base plan, the playout and human play's default are rank 0 of the same
  list the pins index). Executor, rollout and human play read the same ranks off the same state. **ORDER ONLY**: the
  candidate set and its size never change, so the lever is inert at an unlimited budget by construction (pinned:
  the candidate multiset lever on / off is identical, the fast path equals the reference rank for rank).
- **Ponder's shuffle** is always at index 0 or 1 of the scored fan (beside the best keep order; the future wave 0); its
  score only decides which of the two goes first (first only when the look holds no Eldrazi, nothing needed, k >= 2).
- **CycloneProvider** (`MTG_CYCLONE_CANTRIP_ORDER`, EnvOn default ON, per-job heurarm slot): `CycloneGoOff` -> {k, c},
  keep-need tiers, the walk scorer; Ponder uses the SAME scorer (position terms plus the card drawn now; nothing removed).
- **`MTG_CYCLONE_PUTBACK_RULE` rebuilt** as rank 0 of the new score (it turns the scorer on for the put-back even with
  the order lever off, and arms SINGLE). The old tier ranking is deleted (it put Emrakul ON TOP of a Toxic Deluge and
  preferred an Ulamog over Emrakul beside a dead card). Still DEFAULT OFF (Q33 is the USER's).
- **The game log** (log only, NOT folded into the play digest, so every fingerprint stays byte-identical): a Brainstorm /
  Brainsurge resolution now logs a DRAW for each card drawn and put straight back, then `REVEAL "<card> (put back)"`
  with the cards top first (GameLogger `NotePutBack` / `FlushPutBack`, flushed by `GameEngine::ResolveStack`;
  `render_game_log.py` and the replay viewer already render a REVEAL). Example, seed 81154 gi 154 T1: `CAST Brainstorm,
  DRAW Unpredictable Cyclone, DRAW Brainstorm, DRAW Island, REVEAL Brainstorm (put back): Brainsurge, Island`, and T2's
  draw step is the Brainsurge.
- **Scenario harness**: `expect_log_sequence` (an ordered subsequence of the game log's action lines) and `show_log`;
  the scenario now binds the reveal logger the way `RunGame` does.
- `MTG_CANTRIP_ORDER_COST` (diagnostic, default off): the scored order's own cost.

**The go-off estimate as implemented (`CycloneGoOff`).** Reads hand, battlefield, life and the land drop only.
- Missing piece (k = 3, c = 0): no Unpredictable Cyclone in hand or play, no creature cycler in hand, or {3}{R}{R} not
  reached by turn 3. (USER default taken: **a missing piece means k >= 2**.)
- Mana on turn t: t = 0: untapped mana lands + 1 if the land drop is open and an untapped-entering land (a fetch
  counts) is in hand; t >= 1: every mana land we control + one land per turn from hand (an enters-tapped land played
  on turn t is counted that turn: an approximation). Seething Song in hand adds net +2 once 3 mana are available.
  Red: the shared `CycloneShortOnRed`. Cyclone needs 5 when not out.
- k = the first t in 0..3 with the Cyclone payable and a creature cycle payable after it; c = Street Wraith cycles
  (life > 2 before each, 2 life each) + Curator cycles out of the mana left.
- Keep needs per hand card (first copy keeps the high need; the resolver puts back a class's last copies): sole Cyclone
  1000, the lands needed for {3}{R}{R} 300 (surplus 20), Seething Song 300 if needed (else 25), creature cyclers up
  to three 250 (free) / 240 ({U}) and 15 beyond, cantrips / Long-Term Plans 60, other 50, Remand 30, Eldrazi 10,
  Toxic Deluge (goldfish_inert) and a surplus Cyclone 0 (dead).
- Shuffle after the cantrip: the land drop open with a fetchland in hand, Myriad Landscape out with 3+ untapped mana
  lands, or Long-Term Plans in hand with 3+ untapped.

**The score** (lower = earlier). Put-back: each removed card costs its need (a quarter of it when it sits at a position
<= k, i.e. it is drawn back before go-off); both drawn: the more needed one on top; then the walks at go-off: from
position k+1, each of min(c, cyclers left) creature cycles bottoms the noncreatures it passes and scores its first
creature (Emrakul -400, an Ulamog -300, each later walk 10% less so Emrakul goes first; a non-Eldrazi creature +200);
known cards still on top after the walks +2 each; removing a go-off piece at a position past k voids the walks. Shuffle
line: a put-back of dead cards that keeps an Eldrazi in hand is -500 when the branch is admitted (k >= 2, 1-2 dead
cards, a piece missing, a shuffle after) and +30 otherwise -- ORDER only, it stays in the fan either way. Ponder: minus
the need of the card drawn now, minus a quarter of each card drawn before go-off, the same walk terms after them.
- Knowledge: the engine tracks no "seen / placed since the last shuffle" record, so the score reads only the cards THIS
  resolution places (the put-back's two) or reveals (Ponder's three) -- a Long-Term Plans card placed earlier is not
  read. Pinned: different deeper libraries give the identical order.
- USER default taken: **for k = 1 with two Eldrazi, only one is placed for the walk** (Emrakul at position 2); the other
  stays in hand or rides position 1, i.e. is drawn back before go-off (the scorer prefers a dead card there when one is
  held, else the second Eldrazi, since either way it is in hand at go-off).

**Unit tests and gates.** `test_cyclone_cantrip_order.cpp` (8 cases): k / c on hand shapes; keep needs; rank 0 under the
USER's rules (a dead noncreature above Emrakul; two Eldrazi with two cycles both back, Emrakul first; never a non-Eldrazi
creature above the Eldrazi; k = 1 [next-turn card, Eldrazi]; k >= 2 the least needed back); the shuffle-line admission
(dump first with Tarn + drop open, an Eldrazi first without); ORDER ONLY (identical candidate multiset and count lever on /
off, fast path == reference rank for rank, Ponder's 7 dispositions); KNOWLEDGE (deeper library cards never move the order);
Ponder by the same scorer with the shuffle at index 0 / 1; PUTBACK_RULE = rank 0; the generic provider never scores.
Scenarios (expect_log_sequence on the game log, d5 b20): `cyclone_putback_dead_above_emrakul` (T3 win, put back
[Toxic Deluge, Emrakul]), `cyclone_putback_two_eldrazi_emrakul_first` (both cycled, T3 win against 25 life),
`cyclone_brainstorm_dump_then_fetch_shuffle` (Brainstorm draws the missing Cyclone, puts back both Deluges, THEN plays
and cracks Scalding Tarn; T4 win). The last two FAIL with the order off (the fixtures discriminate). Unit 2087/2087,
scenarios 324/324, smoke 149/149 ALL PASS with **play-changed 0 on every deck** (searched and d0). Lockstep: MTG_FD_ORACLE
d5 b20 x80 (seed 103000) 0 `[fd-diverge]`; MTG_FLAG_NONCONV d3 b10 x80 (seed 104000) 0 `[nonconv]`.

**MEASUREMENT (held-out, paired, ONE pooled batch, 20 threads, binary af6539dff; FRESH seeds 101000 (d3 b10 x200,
`--ignore-play-profile`) and 102000 (d5 b20 x100, the play settings); profile attached; MTG_PLAN_CACHE_KB=12000000 -- the
pool still bound (2,970 refused stores, the Q34 [determinism] warning), so the arms are paired but not byte-reproducible
across interleavings).** Arms: A = shipped (order ON), B = order OFF (`MTG_CYCLONE_CANTRIP_ORDER=0`, the pre-item-1 play),
C = order ON + `MTG_CYCLONE_PUTBACK_RULE=1` + `MTG_CYCLONE_PUTBACK_SINGLE=1` (SINGLE under the NEW rule). delta = arm - ref
in avg win turn (unwon = 9; - = faster). Kept games = the first seven kept (mulligan count 0 from a d0 log run of the same
seeds, `mulls.json`): 119 / 200 at d3, 59 / 100 at d5.
| comparison | depth | n | delta (se) | arm slower / faster | units, all games | units, KEPT games | kept-game delta |
|---|---|---|---|---|---|---|---|
| A (order ON) vs B (OFF) | d3 b10 | 200 | **-0.100 (0.053)** | 23 / 36 | x0.937 | **x0.928** (442K vs 476K / game) | -0.176 |
| A vs B | d5 b20 | 100 | **-0.080 (0.081)** | 19 / 24 | x0.957 | **x0.934** (599K vs 642K) | -0.153 |
| C (SINGLE, new rule) vs A | d3 b10 | 200 | **-0.135 (0.055)** | 25 / 41 | x0.220 | x0.195 (86K vs 442K) | -0.101 |
| C vs A | d5 b20 | 100 | **-0.020 (0.062)** | 15 / 17 | x0.220 | x0.199 (119K vs 599K) | 0.000 |
| C vs B | d3 b10 | 200 | -0.235 (0.055) | 23 / 59 | x0.206 | x0.181 | -0.277 |
| C vs B | d5 b20 | 100 | -0.100 (0.081) | 16 / 24 | x0.211 | x0.186 | -0.153 |
Averages: d3 A 5.235 / B 5.335 / C 5.100; d5 A 5.260 / B 5.340 / C 5.240. The order's gain is in the KEPT games (d3 kept
-0.176, mulligan games +0.012: the bottoming trials are butterflies). Wall (thread-ms in the batch, one process, so the
same box load): A 6.70M / B 8.59M ms at d3 (x0.78), 5.95M / 4.82M at d5 (x1.23) -- the d5 wall is ABOVE B despite fewer
units: the order's own scoring cost (below) and the pool binding; not a MTG_PGO=0 wall A/B, so read units, not ms.

**The go-off estimate's own cost** (`MTG_CANTRIP_ORDER_COST=1`, single thread, 8 games seed 7600 d3 b10): 6.76M scored
resolutions (put-back + Ponder), 194M candidates scored, **27.5 s of a 310 s process (8.9% of wall)**, 4.07 us per
resolution / 0.141 us per candidate. Work UNITS are untouched (the scorer is not a search node), so budgeted play does not
pay it; wall does. Every apply of every fan member re-derives the readiness and re-scores the whole fan off the same
state: plan item 8's per-fan ranking cache (keyed on the resolution state) removes nearly all of it.

**Q33 re-measured: SINGLE under the NEW rule** (C vs A, i.e. the prune on top of the shipped order): **d3 -0.135 (se 0.055),
d5 -0.020 (se 0.062)**, units x0.22 at both depths. Against the pre-item-1 play (C vs B): -0.235 / -0.100. The old
measurement (old rule's SINGLE vs the then-shipped set) was -0.105 / -0.110 at x0.27 / x0.25. So the rebuilt rule's
single pick still beats the searched fan at d3 by more than before, but at d5 the ORDER alone now captures most of what
SINGLE bought (A is -0.080 vs B; C adds only -0.020). SINGLE stays OFF (USER decision Q33).

**The slower games, re-salted** (A slower than B: 23 at d3, 19 at d5; each replayed for both arms under
`shuffle_salt_search` 1-4 AND under four REAL futures `shuffle_salt` = `shuffle_salt_search` = k, 1-4 -- the ensemble the
root-cause section showed is the right one; 672 single-game jobs, one pooled batch). Mean (A - B) over the slower games:
d3 +0.043 (search salts) / -0.098 (real salts); d5 +0.250 / +0.079. By the brief's test (search-salt mean > 0) 10 + 11
are "unrecoverable"; only 5 + 5 stay slower under the real futures too, and those ten were replayed single-threaded with
full logs (all reproduce the batch's salt-0 result exactly):
| depth gi | mull | salt 0 A / B | first divergence | class | reading |
|---|---|---|---|---|---|
| d3 91, d5 3, d5 61 | 2 | 5/4, 6/4, 6/5 | the MULLIGAN BOTTOM (d5 61: A bottoms Street Wraith + Remand and keeps two Toxic Deluges; d5 3: A keeps both Deluges) | (c) bottomer | the lookahead bottomer's one clairvoyant trial game per candidate, played under the arm's order -- the known Q32 / root-cause defect 2, not the order's own decision |
| d3 61 | 1 | 7/6 | T2: B cycles both Curators, A holds them (no cantrip in the real game yet) | (c) butterfly | the rollouts' cost / ranks moved the turn's choice |
| d3 96 | 1 | 4/3 | T1: A plays Tarn and HOLDS Brainstorm; B Brainstorms (puts Emrakul back on top, drawn next turn) | (c) budget | both are reasonable at k = 2; d8 b0 below |
| d3 78 | 1 | 5/4 | T2 Brainstorm (k = 3): A puts back [Ulamog Gyre, Deluge] and keeps the Cyclone; B puts back [Cyclone, Ulamog Gyre] | (c) | the same three cards come back either way and A holds the Cyclone a turn sooner (no hand overflow); the slower turn comes from the later search choices on a different future |
| d5 75 | 1 | 6/5 | T2 Ponder [Deluge, Ponder, Watery Grave]: A draws the Grave now, B draws the Deluge | (a) clairvoyance | A's order is the non-clairvoyant one (the land now); B's gain rides the known order behind it |
| d3 151 | 0 | 8/5 | T1 Brainstorm (k = 3): A puts back [Remand, a surplus Myriad Landscape], B [Curator, Brainstorm] | (a) | two non-dominated put-backs at a missing piece; both lines redraw the cards; the futures decide (salts: A 7,8,6,8 / 8,5,5,5 vs B 6,7,7,6 / 7,5,4,5) |
| d5 13 | 0 | 7/5 | T1 Brainstorm (k = 3): A puts back [Remand, Remand] and keeps two Brainsurges; B puts back the Brainsurges | **(b) scorer gap** | at k >= 2 the scorer ranks by keep-need only (Remand 30 < Brainsurge 60) and cannot see that a Brainsurge is UNCASTABLE until the third land -- the USER's Niv rule "followed by the ones you can't use yet so you can draw them later" says put the Brainsurges back. Amendment below |
| d5 5 | 0 | 5/4 (all 8 futures 5 vs 4) | T1 land (Vents vs Island, a butterfly), then decisively T3 Brainsurge (k = 3): A puts back [Ulamog, Deluge] and keeps 8 cards -- and the CLEANUP discard sheds the sole Unpredictable Cyclone; B puts the Cyclone on top (drawn next turn, safe) and sheds the Deluge | **(b) interaction** | the put-back is right only if the cleanup discard is sane: the deck's generic shed order ranks the Cyclone MOST expendable (the same order that is B's put-back order, which is why B "protected" it). Two amendments below |

**Proposed amendments (not built; the USER's call):**
1. **Usable-before-redraw term (from d5 13).** For a put-back card at a position p <= k (drawn back before go-off), charge its
   need only if it is castable before turn p (mana value vs the mana projection the estimate already computes); a card
   that cannot be cast before its redraw costs ~0 to put back. This is the Niv rule's "the ones you can't use yet".
2. **Cleanup overflow (from d5 5).** When the hand left by the put-back is above seven at end of turn (after the casts this
   turn can afford), charge each kept card the need of the card the cleanup would shed -- or, simpler and better,
   give CycloneProvider a cleanup discard that never sheds the sole Cyclone (the Q42 derived order already ranks it among
   the costliest to discard). The generic shed order discarding the deck's only payoff is the real defect.

**d8 b0 (the recovery bar) on the bad-decision / budget games:** d5 gi5: A **recovers** (T4 = B; at d8 b0 the T3 Brainsurge
puts back [Remand, Island] and the cleanup sheds the Ulamog instead -- the d5 b20 loss is rank 0 taken under a binding
budget). d5 gi13 and d3 gi96 (both arms) were still running at d8 b0 when this was written:
`build/scratch/cyclone/cantrip1/runs/d8{A,B}_{d5_13,d3_96}` (marker `runs/D8_DONE`).

**The dominated T1 land skip** (root-cause defect 1: seed 81130 gi130, d3 b10, `--ignore-play-profile`, holding Watery
Grave + Myriad Landscape, no land): under the new order it **does not recur** (T1 Watery Grave, win T4); with
`MTG_CYCLONE_CANTRIP_ORDER=0` on the same binary it still skips the land (win T5). The T1 hand holds no cantrip, so the order
changed it only through the rollouts' cost: still a budget-tie artifact that this seed no longer trips, not a fix.

**Verdict.** The order is an ORDER (complete at unlimited budget, pinned) and measures better at both depths on fresh seeds:
d3 -0.100 (se 0.053, t ~ -1.9), d5 -0.080 (se 0.081), kept-game units x0.93; it ships ON as the brief and the design
direct. The rebuilt SINGLE adds -0.135 at d3 but only -0.020 at d5 on top of it. Two scorer gaps surfaced (amendments 1-2),
both visible only under a binding budget (d5 gi5 recovers at d8 b0).

### Remand ceiling (2026-10-28)

USER 2026-10-28 (plan: `logs/cyclone/remand-plan.md`): test whether a higher hand-cast Remand ceiling (2 or 3) is worth
anything (*"though I doubt we would want to Remand very expensive spells"*); *"we should allow casting Remand to be
Remanded if MV 2 is supported ... the first one targeting anything and the second targeting Remand"*; Toxic Deluge cast
only to be Remanded if MV 3 is tried (no "only if we run out of everything else" condition: the search decides);
*"with clairvoyance I'm sure the search will choose to do this some of the time just to change what is on top ... the
only question is whether we want to actually retain those lines"* -> raw delta, blind-replay delta and units, the
decision the USER's. **The shipped default stays 1.**

**Built** (commits 4369e1e2a code, 1f51a29c9 tests, f789f4182 DECISIONS.md):
- REMAND-ON-REMAND. `Action::remand_chain_by` (TurnSolver.h:751; CastRiders twin :943): Remand A on X, Remand B
  (`remand_by`) on A. B resolves first (A back to hand, B draws), then X resolves normally. One body for both worlds,
  `CastAndResolveRemandChain` (ManaPayment.cpp:6158), one liveness predicate `RemandChainLive` (:6138: two copies besides
  X, three casts left; else the whole rider is void and X is cast plainly). Executor AIEngine.cpp:9667, rollout
  TurnSolver.cpp:57498. Offered by CollectActions' remand post-pass (TurnSolver.cpp:42702): A's target is the
  STRUCTURAL rider set with NO deck ceiling (USER: "the first targeting anything"); one modelling restriction: X's own
  resolution may open a breakpoint only as a PLAIN CANTRIP (Ponder / Brainstorm / Brainsurge: that one breakpoint, after
  both draws, is the cast's -- AIEngine `BpNameOf` :2820 and apply_one skip the Remand's), never another draw class (a
  tutor-to-hand, an impulse, a cascade), where two breakpoints in one cast are inexpressible (Q49). Free cast:
  `kCycFcTakeRemandChain` (SpellEffects.h:40227) + `CycleFreeCastRemandChain` (:40260; no `AdmitRemandOnFreeCast` prune
  -- A is countered, the hit resolves), the seed pin (TurnSolver.cpp:41636), the executor (AIEngine.cpp:8628). Human
  play: the menu label "X + Remand on it + Remand on that Remand (X resolves; ...)", CheckLine absorbs both declared
  Remands, and a further `free_cast` prompt on the walk (TurnSolver.cpp:59137).
- `DecisionProvider::AdmitRemandOnRemand` / `AdmitRemandOnlyCast` (DecisionProvider.h:968/974), both DEFAULT FALSE, so
  Hinata2 / NivMizzetCombo keep their play (Q48). CycloneProvider (DecisionProviders.cpp:30595/30614): the chain when
  the ceiling reaches Remand's MV 2; a goldfish_inert card under the ceiling behind `MTG_CYCLONE_REMAND_INERT` (per-job
  `valuearm::Arm::cyclone_remand_inert`, manifest "cyclone_remand_inert"; ValueArm.h:174).
- TOXIC DELUGE as a REMAND-ONLY target (TurnSolver.cpp:42669): keyed on goldfish_inert + the rider, never the name; a
  base cast built only to carry the rider (never pushed alone), never a chain target (it must not resolve). X = 0 life is
  paid and the sweep is never realised. (A VOID rider -- the Remand gone before the cast -- would cast it plainly at X =
  0, a legal do-nothing, identical in both worlds.)
- Tests: test_cyclone_remand_chain.cpp (8 cases: gates, enumeration per ceiling, lockstep rollout == executor for the
  chain on Seething Song and on Ponder, a void chain, Deluge -> Remand, the free-cast chain on a Curator AND an Ulamog
  hit, the seed pin). Six scenarios: the chain on a hard-cast Cyclone draws the Wraith -> Emrakul round 5 at ceiling 2 vs
  round 6 at ceiling 1; Deluge -> Remand round 5 (3 + switch) vs round 6 (3 without); the human line
  `cast=Seething Song;cast=Remand;cast=Remand` matched at 2, `legal_not_enumerated` at 1. Unit 2095/2095, scenarios
  330/330, **smoke 149/149, play-changed 0**. Ceilings -1 and 1 BYTE-IDENTICAL to 1061c1fb4: 120/120 sample games (d3 x40
  and d5 x20 per arm) match in win turn AND per-game play digest.

**Measured** (one pooled batch, frozen binary at 4369e1e2a, 18 threads; seeds 87000 d3 b10 `ignore_play_profile` x400
and 88000 d5 b20 profile depth x200 -- the earlier totals-only run's seeds; scratch `build/scratch/cyclone/remand2/`:
manifest.json, batch.out/.err, logs/*.wins, traces/, analyze.py). Delta = arm minus ceiling 1, paired, loss-penalized
(unwon = 9); + = slower.

| ceiling | d3 avg | d3 delta (se) | d3 slower/faster | d3 units | d5 avg | d5 delta (se) | d5 slower/faster | d5 units |
|---|---|---|---|---|---|---|---|---|
| -1 | 5.2850 | -0.0125 (0.0148) | 12 / 15 | x0.946 | 5.1650 | -0.0100 (0.0235) | 5 / 5 | x0.922 |
| 1 (shipped) | 5.2975 | - | - | - | 5.1750 | - | - | - |
| 2 (+Remand-on-Remand) | 5.3050 | +0.0075 (0.0139) | 7 / 8 | x1.028 | 5.1850 | +0.0100 (0.0158) | 6 / 4 | x1.018 |
| 3 without Deluge | 5.3125 | +0.0150 (0.0158) | 11 / 10 | x1.050 | 5.2100 | +0.0350 (0.0148) | 8 / 1 | x1.038 |
| 3 with Deluge | 5.3075 | +0.0100 (0.0150) | 12 / 10 | x1.055 | 5.1950 | +0.0200 (0.0187) | 9 / 5 | x1.037 |

TAKE RATE (games with the line / lines, from the executor's game traces). Ceiling 1: hand-cast single Remand d3 24 games
/ 25 (Ponder 18, Brainstorm 7), d5 6 / 6; free-cast single d3 38 / 43, d5 19 / 19 (every arm similar). Ceiling 2:
**Remand-on-Remand on a hand cast d3 37 / 38** (Ponder 12, Brainstorm 10, Long-Term Plans 4, Seething Song 4,
Brainsurge 4, Curator 4), **d5 17 / 18** (Brainstorm 6, Brainsurge 6, Ponder 3, LTP 2, Song 1); on a free-cast hit d3 1
(Ulamog, the Infinite Gyre), d5 2 (Emrakul 1, Gyre 1). Ceiling 3: chains d3 34 + 1, d5 18 + 2; the new MV-3 singles are
rare (d3 4 lines: Song 3, LTP 1; d5 3). 3 with Deluge: **Deluge -> Remand d3 26 games / 30 lines, d5 15 / 19**; chains
d3 30 + 1, d5 16 + 1. Of the faster games, the new line was played in c2 d3 4/8, d5 0/4; c3 d3 5/10, d5 0/1; c3d d3 5/10,
d5 2/5 -- the rest are butterflies (a different budget spend, the same line).

**Blind replay of every FASTER game** (each arm's faster games AND ceiling 1 on them; salt 0 reproduction,
`shuffle_salt_search` 1-4, and four real futures `shuffle_salt` k with search salt 1000 + k; 828 one-game jobs, one
pooled batch; `resalt_manifest.json`, `resalt.out`, `resalt_report.py`). All 58 (arm, game) pairs reproduce at salt 0.
Turns per game over N (400 / 200); + = the arm slower:

| arm | depth | faster games | raw gain | surviving, search salts | surviving, real futures |
|---|---|---|---|---|---|
| 2 | d3 | 8 | -0.0200 | +0.0006 | +0.0013 |
| 3 | d3 | 10 | -0.0250 | -0.0006 | +0.0006 |
| 3 + Deluge | d3 | 10 | -0.0275 | -0.0031 | +0.0050 |
| -1 | d3 | 15 | -0.0425 | -0.0006 | +0.0019 |
| 2 | d5 | 4 | -0.0200 | +0.0037 | 0.0000 |
| 3 | d5 | 1 | -0.0050 | 0.0000 | 0.0000 |
| 3 + Deluge | d5 | 5 | -0.0250 | +0.0013 | -0.0013 |
| -1 | d5 | 5 | -0.0400 | 0.0000 | +0.0037 |

**Reading.** NONE of the faster games' gain survives blind replay: under the search salts (real future unchanged, the
search's post-shuffle knowledge blinded) and under other real futures the two arms tie in almost every game, so the
raw gains are the realised future, i.e. clairvoyance / shuffle luck (the USER's expectation). With the faster games
replaced by their blind value, every wider arm is WORSE than ceiling 1 at both depths (raw delta minus the faster games' raw
gain plus their blind value, search / real salts: 2: +0.028-0.029 d3 / +0.030-0.034 d5; 3: +0.039-0.041 / +0.040; 3 + Deluge:
+0.034-0.043 / +0.044-0.046 -- an upper bound: the slower games were not replayed, Q51) and costs
2-5% more units; the -1 arm's 20 faster games likewise vanish (-1 blind: +0.029-0.032 / +0.030-0.034 vs ceiling 1, also
un-replayed on its slower side). The lines ARE taken (chains in ~9% of games, Deluge -> Remand in 7-8%), mostly on the
cheap cantrips, i.e. the search spends four mana to cycle a Remand when it sees the card it draws. Recommendation
(the USER decides, Q47): keep the shipped ceiling 1 and leave the chain and the Deluge switch off.

### Cantrip scorer: item 8 cache + Q44(1) (2026-10-28)

**Item 8, the per-fan ranking cache (a pure speedup; byte-identical).** Every searched member of a put-back / Ponder
fan re-applies the same prefix, reaches the SAME resolution and used to re-derive the readiness and re-score the whole
fan (Q46). Built (`core/SpellEffects.h` `cantriporder::RankCache`, `DecisionProvider::CantripOrderKey`,
`CycloneProvider::CantripOrderKey`): a per-thread, direct-mapped (256 slots) cache of the scored order -- the put-back's
sorted permutation, Ponder's per-candidate scores -- keyed WORD FOR WORD (the hash only picks the slot) on the
provider's declared inputs (our permanents by interned name + tapped + def_absent, the hand in order with P/T and name
hash, life, land drop played / available, the per-job flags) plus the candidate list (put-back: take, count and every
(top, second) hand-index pair in the existing order; Ponder: the looked cards, draw_now, every base disposition and the
library top three the shuffle's score reads). A provider without a key (every deck but Cyclone) never reaches it.
`MTG_CANTRIP_RANK_CACHE=0` turns it off; `MTG_CANTRIP_RANK_CACHE_VERIFY=1` re-derives every hit and aborts on a
mismatch. Not built: the design row's other half, the PREFIX SNAPSHOT (skipping the w-1 prefix re-applies), which is
a search-side change.

*Byte-identity.* Pre-change binary (HEAD f5fe48a8d, built from a clean `src/`) vs the cache build, the same manifest
(d3 b10 x120 seed 101000 `--ignore-play-profile`, d5 b20 x120 seed 102000 at the play settings), run CONCURRENTLY (5
threads each, the same box state), `MTG_PLAN_CACHE_KB=0` (= UNBOUNDED for the plan cache -- not "off", see the note below
-- so no pool refusals and the runs are deterministic): **240 / 240 per-game digests, win turns and work units identical**
(`d3.wins` / `d5.wins` / `.units` byte-equal; job digests d3 f023f43f1acfb34c, d5 e54967469a8fa241 on both). VERIFY
(`MTG_CANTRIP_RANK_CACHE_VERIFY=1`, 6 games seed 7700 d5 b20, single thread): 8,524,841 hits each re-derived, 0
mismatches. Unit: a new case warms the cache and checks nine one-input variations (a land tapped, the drop, life, a bonus
drop, the hand order, a permanent removed / added, a Cyclone out, the per-job flags) rank-for-rank against the uncached
reference enumeration.

*Wall* (MTG_PGO=0 is a harness setting; both arms are Release binaries run directly). Pooled batch above: process wall
1835.5 s -> 1782.6 s (**-2.9%**); summed per-game thread-ms d3 2.874M -> 2.929M (+1.9%), d5 5.895M -> 5.496M (-6.8%),
total 8.768M -> 8.425M (**-3.9%**) -- a noisy reading: the other worktree's compiles and one of mine (4 jobs) ran during
it. The scorer's own cost, `MTG_CANTRIP_ORDER_COST=1` on the Q46 probe (8 games seed 7600 d3 b10, single thread, both
binaries concurrently): **14.57 s -> 8.52 s** (2.57 -> 1.51 us per resolution; 9.9% -> 6.1% of the process), **hit rate
91.8%** (5,196,027 of 5,661,831 resolutions); process wall 146.7 s -> 139.0 s (**-5.2%**). The residual 1.5 us per
resolution is the key itself (the walk that lists the fan, ~80 key words, one allocation) and, for Ponder (~20% of the
resolutions), the base list (the provider's pick + the seven enumerated dispositions), which is still built before the
lookup because the provider's pick may read any state; caching it would need the provider to key that read too.
Note: `MTG_PLAN_CACHE_KB=0` means UNBOUNDED, not off (`plancache::Fits`: `b <= 0` admits every store; the comment above
`plancache::BudgetBytes` reads "=0 = off"). A 10-thread Cyclone batch reached a 31.6 GB plan pool in under two minutes
and the rss-cap watchdog killed it; the A/B below ran at `MTG_PLAN_CACHE_KB=10000000`. The item-7 probe in the design
(`MTG_PLAN_CACHE_KB=0`) is therefore an unbounded-cache probe.

**Q44(1), the usable-before-redraw amendment (`MTG_CYCLONE_PUTBACK_USABLE`, per-job heurarm slot; built DEFAULT OFF).**
In the put-back score a card at a position p <= k (drawn back before go-off) is charged a quarter of its keep need; with
the switch on it is charged 0 when it cannot be USED before it returns: a land when no land drop is open before turn p,
anything else when its cheapest use (mana value, or its cycling cost's) is above the mana the go-off estimate projects
for turn p - 1 (`CycloneGoOff` now records the projection for t = 0 and 1 and the open drop in `aux`; `aux` grew 12 ->
16). Only when no shuffle is available after the cantrip (a shuffle would lose the card). An ORDER only. Unit
(`test_cyclone_cantrip_order.cpp`, 1 case / 4 subcases): gi13's T1 state (k = 3, Vents tapped, drop used) puts back
[x, Brainsurge] with no Remand when on and [Remand, Remand] when off; the candidate multiset is unchanged; a shuffle
after the cantrip and a fully castable hand leave the old order in force. Scenario
`cyclone_putback_unusable_before_redraw` (gi13's position, d5 b20, `env` turns the switch on): `REVEAL Brainstorm (put
back): Brainsurge, Brainsurge`; it FAILS with the switch off. The motivating game itself (seed 102013 gi13, d5 b20,
single game): off T7 (reproduces the ledger), **on T5** (it now puts back [Scalding Tarn, Xander's Lounge] -- two lands
it cannot play before they return).

*Held-out paired A/B* (ONE pooled batch, 14 threads, binary = the item-8 build + the amendment; FRESH outcome seeds
103000 d3 b10 x400 `--ignore-play-profile` and 104000 d5 b20 x200 at the play settings -- used before only as the
lockstep checks' seeds, whose outcomes were never read; profile attached; `MTG_PLAN_CACHE_KB=10000000`, which bound
(4,576 refused stores: paired, not byte-reproducible across interleavings; every slower game below reproduced exactly as
a single game)). delta = on - off in average win turn (unwon = 9; + = on slower):

| depth | n | off | on | delta (se) | on slower / faster | identical games | units on/off |
|---|---|---|---|---|---|---|---|
| d3 b10 | 400 | 5.2175 | 5.2325 | **+0.015 (0.027)** | 41 / 38 | 101 | x1.000 |
| d5 b20 | 200 | 5.2100 | 5.2550 | **+0.045 (0.039)** | 23 / 18 | 43 | x1.015 |

*The 64 slower games, root-caused* (each replayed as a single game for both arms with full logs, `rep.sh`; all 64
reproduce the batch's win turns; first divergence of the two action logs, `classify.py`; `classify_d3.txt` /
`classify_d5.txt`):
| first divergence | d3 | d5 | mechanism |
|---|---|---|---|
| a root decision BEFORE any put-back differs (a land, a cast, a cycle, Ponder's keep/shuffle) | 29 | 16 | (c) budget butterfly: the amended rank 0 changes every rollout's put-back, so the root's evaluations move and a different (near-equal) line is taken under the binding budget |
| a draw order after identical visible actions (a fetch target / crack timing the log does not show) | 7 | 3 | (c) the same, through an unlogged decision |
| the put-back itself | 5 | 4 | below |

The nine direct put-back divergences show the amendment's mechanism. It puts back the cards it cannot use before the
next draw step -- the SOLE Unpredictable Cyclone (d3 gi12, gi289, gi392: [Cyclone, Island] on top), a Curator of
Mysteries whose {U} cycle is unaffordable this turn (d3 gi153, gi253; d5 gi64, gi101, gi103), two Ulamogs (d5 gi184).
In the pure card-flow model these are EQUIVALENT to the old pick: both put-back cards are drawn again by turn p, so by
then the hand is the same either way; only what is in hand during the gap differs, and the amendment is right that
nothing in it could be used. The slower results come later: the following turns' search choices differ (gi64: off
cycles Curator + Wraith on T2, on holds them), and in d5 gi103 the tie broke toward keeping Xander's Lounge (enters
tapped) over Steam Vents, so T4 had one mana less and could not cast Ponder + Long-Term Plans -- a tapped-vs-untapped
land tie the score does not see. The structural finding: **charging 0 flattens most of an early put-back fan to the
same score, so rank 0 is decided by the existing order's tie-break, which is the generic shed order** (the order that
ranks the sole Cyclone most expendable, Q42 / Q44(2)). The old score's need/4 kept a need ordering among those cards.

*d8 b0 recovery:* not run. The adoption bar applies to an adoption, and the switch is not adopted; the amendment is an
ORDER (the candidate set is unchanged), so at an unlimited budget it differs from the shipped order only through ties.
(The cantrip-item-1 d8 b0 repros of d5 gi13 / d3 gi96 were killed by the main session after 5 h 06 m at 3.5-9.7 GB RSS
each without finishing: **d8 b0 is not tractable on these Cyclone games**; a future check should cap each game at ~90 min
and fall back to d7 / d6 b0, in one pooled batch.)

**Verdict (the user may veto): Q44(1) is NOT adopted -- shipped DEFAULT OFF.** It fixes the motivating game (gi13 T7 ->
T5) but is worse, not better, on the held-out sample at both depths (d3 +0.015, d5 +0.045; neither significant, t 0.55
/ 1.15), at equal units. The proposed refinement (Q52) keeps the need ordering: charge an unusable early card a small
residual (e.g. need / 16) instead of 0, so unusable cards still go back before usable ones but rank among themselves by
need, and the Cyclone / a cycler no longer ties with a dead card. Not built or measured here.

Artifacts: `logs/cyclone/item8/` (identity batch outputs + per-game `.wins` / `.units`, the cost probe, VERIFY) and
`logs/cyclone/q44/` (the A/B batch, `report.py` / `report.json`, `classify.py` and the per-depth first-divergence
listings); single-game logs under `build/scratch/cyclone/q44/rep/`.

### Sound folds F2/F4/F5/F3 + Q36 (2026-10-28)

Design: `docs/design/cantrip-search-optimization.md` §2 (F2-F5) and §6 rows 3-5; Q26 (a) / Q36 below. Every fold is a
per-job lever (heurarm) with a VERIFY mode that applies what it would decline and compares; the sound ones ship
DEFAULT ON (CLAUDE.md "collapse wasted search unconditionally"), the one that is not exact ships OFF. Commits:
865529ba0 (F2), 9618b2ba6 (F5 + the F4 candidate + the shared fan-table channel + an F2 guard), 506ba7dc1 (F3), 9b384907d (Q36).

**What was built.**
| fold | lever (default) | what it removes | why it is exact |
|---|---|---|---|
| F2 exact fan width | `MTG_PB_EXACT_WIDTH` (ON) | the clamped ranks past the real put-back list: the fan is the resolution's own count when nothing that changes hand or library is applied before the put-back (PutBackExactWidth: the hand minus the cast and a land-first inert land, plus the top `draw` cards, the resolver's class counter) | the same candidates in the same order; only duplicates of the last one go. A pure ritual ahead of it may still be DROPPED by the apply (the Q37 phantom-ritual credit), so it counts as still in hand: a tight upper bound, not exact |
| F3 shuffle mirror | `MTG_PB_MIRROR_FOLD` (ON) | the second of (a, b) / (b, a) when the put-back's first library event is a shuffle (a library tutor cast next, or trailing land-search activations: Myriad Landscape) | ShuffleByKey orders by card NUMBER, so one set shuffles to one order -- provided the search before the shuffle cannot see the swap (at most one of the pair's classes findable by it), every number in library + hand is distinct, and the publishing apply witnessed the shuffle |
| F4 stranded cast | `MTG_PB_STRAND_FOLD` (**OFF**, not exact) | a rank that puts back the last copy of a card the plan casts later (before any later cast that can draw) | it realises the smaller plan's same-pair member -- up to the payment allocation, which turned out NOT to be all (below) |
| F5 cross-base prefix | `MTG_PB_XBASE_FOLD` (ON) | at a breakpoint-node host (FSLineWin), the whole fan of a base whose truncated prefix pended where an earlier base with the SAME prefix pended | the identical apply up to the partition at every rank (same actions, same resolution, same ordinal; the rank-0 collision witnesses that the tails' bills did not steer the prefix's payment) |
| Q36 seed-only walk | `MTG_CYCLE_SEED_WALK_FOLD` (ON) | a cycle-seed variant of a SEED-ONLY plan with Unpredictable Cyclone out whose Curator scry + walk + keyed bottoming leave an earlier sibling's state (same pin), or any variant of a whiffing walk | the walk is computed exactly off the known library (the enumeration's state is the seed's); the random bottom is a keyed order of the set (KeyedBottom = BottomInRandomOrder's twin); the realised free-cast branch depends on the hit and the board only |

**Verify modes (all mismatches must be 0).** Samples: Cyclone d3 b10 `--ignore-play-profile` x8 (seed 107000) and x16
(107100), d5 b20 at the play settings x8 (108000), one pooled batch with every verify on; then the F2 re-check.
| fold | compared | mismatches | notes |
|---|---|---|---|
| F2 | 1,924,815 + 6,192,156 resolutions | 0 / **3,224 then 0** (997,190 after the guard) | the 3,224 were all OVERcounts (emitted > real: harmless duplicates), every one the number-0 defect below: casting a number-0 Brainstorm erased a number-0 Ponder. Guard added: distinct card numbers in hand |
| F3 | 48,978 + 8,739 mirror pairs | 0 (after the refinements; the design's version had **2,696**) | the shuffle ties number-0 cards (std::sort keeps the input order of ties) |
| F4 | 252,185 + 45,201 pairs | **549 + 64 OTHER** (and 104,117 / 19,417 payment-only, 147,519 / 25,720 identical) | not exact -> OFF; a few of the 549 are a matching artifact (pairs keyed by card number, number-0 cards ambiguous) |
| F5 | 3,796 ranks | 0 | |
| Q36 | 1,237 variants | 0 | checked with the ENGINE's helpers on two copies |
Fire rates in those 24 games: F2 sized 461,962 fans exactly (129,916 of them tight bounds) and left 147,501 on the
old bound; F3 found mirror-eligible ranks in 2,445 of 134,639 fan tables; F5 41 collapsed fans; Q36 1,237 variants.

**What the verify runs found beyond the folds.**
1. **The number-0 card identity defect (pre-existing; Q53).** The rollout files a resolved instant / sorcery as its
   DEFINITION's template card (`ap.graveyard.push_back(UnkickedSpellCard(def, def.card))` in apply_one; the
   template's m_number is 0), not the cast copy (the Green Sun's Zenith shuffle had the same bug and was fixed by
   GszShuffleCopyIdOn). An Eldrazi's graveyard shuffle (`put_into_graveyard_shuffles_graveyard`) then deals several
   number-0 cards back into the library and the hand. Two consequences, both measured: (a) apply_one re-finds the
   cast copy BY NUMBER after its payment (`cast_zone_number`), so casting a number-0 Brainstorm ERASED a number-0
   Ponder -- the rollout's hand diverged from the real one (d5 T8, seed 108000); (b) ShuffleByKey's order is a total
   order only over distinct numbers; number-0 cards tie and keep their input order, so a "shuffle" is not blind to
   what came before it. The executor files the real copy, so after such a shuffle the clairvoyant search predicts a
   different library / hand than the game deals. Not fixed here (it moves play wherever a graveyard comes back);
   the three folds that rely on card identity (F2, F3, Q36) now require distinct numbers.
2. **F3 is narrower than the design.** The shuffle is order-blind, but the SEARCH before it is not: a tutor's
   candidate list is in library order and it takes the topmost copy of a name; a land search likewise. So the fold
   needs at most one of the pair's classes findable by each search in the window -- Long-Term Plans, which finds
   any card, qualifies only when its target is name-bound. Built plan-level only; the node-level form (the USER's
   Brainstorm-then-crack-Scalding-Tarn line) needs continuation identity matched across two parents' lists: not
   built (Q55).
3. **F4 is not exact.** The payment allocation is steered by the plan's WHOLE bill (the line hold, PlanTraits),
   the stranded cast's bill included, and a different allocation changes what the rest of the line achieves:
   "Seething Song, Brainstorm, Ponder, cycle Curator of Mysteries" with the Ponder put back kept an Island up and
   cycled the Curator; the smaller plan "Seething Song, Brainstorm, cycle Curator" tapped that Island for the Song
   and could not pay the cycle. That line exists only through the stranding member, so declining it is lossy
   under the engine's own model. Shipped OFF; measured as an arm below (Q54).

**MEASUREMENT (held-out, paired, ONE pooled batch).** ONE pooled batch, one frozen binary (sha1 recorded; byte-identical to
the rebuilt head 9b384907d), 8 threads, MTG_PGO=0 (a Release binary run directly), `MTG_PLAN_CACHE_KB=5000000` (the pool
bound: paired, not byte-reproducible across interleavings, Q34), MTG_SLOW_GAME_MS=1, game logs. FRESH seeds (unused in
this ledger before): 105000 d3 b10 `--ignore-play-profile` x400 and 106000 d5 b20 at the play settings (profile) x200.
Arms as per-job flags: ON = the shipped set (F2, F3, F5, Q36 on; F4 off), OFF = all five off, F4 = ON + the F4 candidate.
Kept games (the first seven kept: d0 log runs of the same seeds) 234 / 400 at d3, 123 / 200 at d5. delta = arm - ref in
average win turn (unwon = 9; + = the arm slower); "same" = equal win turn; units = work units.
| comparison | depth | n | avg arm / ref | delta (se) | slower / faster / same | identical play digest | units (all) | units (kept) | kept delta (se) |
|---|---|---|---|---|---|---|---|---|---|
| ON vs OFF | d3 b10 | 400 | 5.2375 / 5.2325 | **+0.0050 (0.0071)** | 3 / 2 / 395 | 390 | **x0.978** (1.594M vs 1.629M / game) | **x0.975** (414K vs 425K) | +0.0043 (0.0113) |
| ON vs OFF | d5 b20 | 200 | 5.1400 / 5.1450 | **-0.0050 (0.0050)** | 0 / 1 / 199 | 196 | **x0.967** (1.861M vs 1.924M) | **x0.980** (594K vs 606K) | -0.0081 (0.0081) |
| F4 vs ON | d3 b10 | 400 | 5.2400 / 5.2375 | +0.0025 (0.0083) | 3 / 2 / 395 | 391 | x1.003 | x0.998 | -0.0085 (0.0105) |
| F4 vs ON | d5 b20 | 200 | 5.1500 / 5.1400 | +0.0100 (0.0071) | 2 / 0 / 198 | 194 | x1.006 | x1.011 | 0.0000 |
The differing games: ON slower d3 gi 28, 136, 343, faster gi 65, 84; d5 faster gi 33. Fold activity in the batch (the ON
and F4 arms; process totals): F3 declined 8.78M mirror ranks (479K fans with mirror-eligible ranks of 10.78M tables), F5
78K ranks (1,107 fans), Q36 116K seed variants (2,174 of them whiffs), F4 (its arm) 8.17M ranks; F2 sized 44.2M fans
exactly (6.6M tight bounds). Process peak RSS 10.3 GB (8 workers, the 5 GB pool bound); wall 3 h 39 m (one F4 game, gi
367, ran 35+ min: a wall pathology of that game in every arm -- ON 33 min, OFF 22 min, at ~3.8M units, 17x the usual
ms per unit -- not a fold).

**Wall.** The batch's per-game thread-ms (MTG_PGO=0, one binary) reads ON/OFF x1.218 at d3 and x0.965 at d5 -- NOT a fold
effect: the pooled arms ran at different times on a box whose load swung 7-37 (my gates, two other agents' batches),
and the d3 figure is dominated by gi 367 (33 min ON, 22 min OFF, the same ~3.8M units). The controlled reading is the
concurrent two-process probe below: -10% process wall. 

**Peak RSS per arm** (the two arms as two CONCURRENT single-thread processes on the frozen binary, the plan-cache pool
UNBOUNDED -- `MTG_PLAN_CACHE_KB=0` -- 6 games d3 b10 seed 109100, env levers): ON **1.71 GB** vs OFF **2.45 GB (-30%)**,
process wall 3 m 28 s vs 3 m 52 s (-10%), both averaging 6.00. Most of the memory is presumably F2's: exact widths mean
fewer clones in every list that is still materialised (the breakpoint continuation cache, the m2 memo); the declines
(F3 / F5 / Q36) save applies, not storage.

**`--budget-ms 0` (unlimited), folds ON vs OFF in one batch (seed 109000).** d3 b0 is INTRACTABLE on this deck: with the
lookahead bottomer on, no game finished in 30 min on 4 threads (each mulligan plays every candidate bottom as an
unlimited game); with it off (`MTG_BOTTOM_ROLLOUTS=0`, both arms), ten d3 games ran 50 min without finishing. Fell back
to d2 x8 and d1 x20 per arm, cheap bottoming on both arms, a 45-minute per-game wall cap (`MTG_MAX_GAME_WALL_SEC=2700`;
a capped game is UN-RUN, never a pass). At the time of writing: **d2 -- 3 pairs completed and equal** (gi 4, 5, 6: T5,
T4, T3 in both arms; ON 131 / 92 / 1 s vs OFF 158 / 194 / 0 s), **0 different, 5 capped (UN-RUN)**: gi 0, 1, 2, 7 capped
in both arms, gi 3 capped in OFF only (ON finished T5 in 21 min at 30.1M units; OFF was cut at 45 min, 49.9M units).
**d1 -- 0 pairs completed (UN-RUN)**: the ON arm's games were still running or done (gi 1, 2, 5, 6, 7, 8, 10, 11, 13
finished) when the OFF arm had barely started; the batch was then STOPPED (2026-10-29 05:39 UTC, the coordinator's
call: an agent experiment, no d1 pair finished). Final counts: d2 3 pairs equal, 0 different, 5 capped; d1 0 pairs.
So the unlimited-budget claim rests on
the three equal d2 pairs plus the verify modes' identities; it is NOT a 20-game confirmation.

**Verdict.** F2, F3, F5 and Q36 are exact by their verify modes. After the guards there were 0 mismatches over about 9M F2
resolutions, 57.7K F3 mirror pairs, 3.8K F5 ranks and 1.2K Q36 variants. They ship DEFAULT ON. As a play change they are
neutral within noise on fresh seeds: d3 +0.005 (se 0.007, 3 slower / 2 faster, 390 of 400 games playing the identical
line) and d5 -0.005 (se 0.005, 0 / 1, 196 / 200 identical). Units are x0.978 / x0.967 (kept games x0.975 / x0.980), peak
RSS per worker is -30% and the controlled process wall is -10%. They do not reach the cost gate on their own: the put-back
fan's width is still the search's cost (Q33's SINGLE and the B1/B2 budget shapes are the levers that move it). F4 is not
exact: 613 of ~297K verified pairs realise a different line, and its arm is slightly worse (d3 +0.0025, d5 +0.010) at
equal units. It stays OFF (Q54). The CLAUDE.md adoption bar's d8 b0 recovery of the slower games was not run, because d3 b0
is already intractable here (above). The differing games are read from the paired digests (99% identical) as budget
butterflies, not as evidence either way.

Artifacts: `logs/cyclone/folds/` (verify outputs, the A/B report and per-game `.wins` / `.units`, the budget-0
check); scratch `build/scratch/cyclone/folds/`.

## Reference games (2026-10-28)
The USER hand-played the deck's first 10 reference games in the play viewer (primary checkout,
`references/Unpredictable_Cyclone/claude_s{1..10}_gi{0..9}.json`, committed 3a1b348a4 and on origin/references-autosave;
copied -- not moved -- onto branch `cyclone-refs` for the replay). USER: *"Many of them I'm sure the clairvoyant search can
beat since this deck is much much easier to pilot with clairvoyance."* *"We should ensure that, at least, the references can
be matched."*

**Replay** (`viewer_protocol_check.py --only Unpredictable_Cyclone --strict`): all 10 replay to their recorded result, 0
play-drift / 0 board-diverged / 0 enum-gap, before and after the changes below (after them the human menus hold the cycle
seeds, so the recorded indices are re-anchored by content: "repaired"). The whole corpus (678 refs) stays 0 / 0 / 0.

**Match** (`ref_bench.py --deck unpredictable_cyclone`, shipped policy d5 b20 from the profile, forced opening hand, every
hand verified). Identical before and after the fixes:
| ref | human | search | d8 b0 | verdict |
|---|---|---|---|---|
| s1 gi0 | 5 | 5 | n/a | match |
| s2 gi1 | 7 | 4 | n/a | search faster (-3) |
| s3 gi2 | 5 | 4 | n/a | search faster (-1) |
| s4 gi3 | 7 | 6 | n/a | search faster (-1) |
| s5 gi4 | 5 | 5 | n/a | match |
| s6 gi5 | 8 | 5 | n/a | search faster (-3) |
| s7 gi6 | no win | 4 | n/a | the human did not win; the search wins in round 4 |
| s8 gi7 | 5 | 5 | n/a | match (the manual-tap game, below) |
| s9 gi8 | 6 | 5 | n/a | search faster (-1) |
| s10 gi9 | 5 | 5 | n/a | match |
Human avg 6.20 (no win = 9), search 4.80: **0 / 10 shortfalls**, 6 faster, 4 equal. No game needed the large-budget
findability check (the column is for shortfalls). (The search is clairvoyant; the human was not.)

**The manual tap (s8 gi7, decision 15, T4) -- root cause and fix.** Board: Island, Island, Mountain, Mountain; hand Curator
of Mysteries, Seething Song, Street Wraith, Ulamog, two Unpredictable Cyclone. The human's line: Seething Song (RRRRR) pays
Cyclone, then cycle Curator ({U}) into the walk (Street Wraith, cycled again into Emrakul -> extra turn -> round-5 kill).
Auto-payment (replayed without the `tap=` tokens) paid the Song with **Island, Island, Mountain** and left a Mountain, so the
Curator cycle was not even offered at the next frame; the human tapped Mountain, Mountain, Island by hand. USER: *"My manual
tap mattered because we left up red when I needed blue for the cycle."* The cycle was a separate, later plan in human play,
so nothing the payment could see said blue was still owed -- and the SEARCH had the same hole: its plan "Song, Cyclone, cycle
Curator" (a cycle seed, paid after the casts) silently no-oped on the stranded payment, in rollout and executor alike, at
every budget (a missing-line defect, not a budget shortfall).
- **Fix 1 -- the cycle seed holds its colour** (all worlds, lockstep). `PlanTraits::cycle_seed_pips` (from the card's
  EffectiveCyclingCostFor, never Action::cost, so the executor's traits over its recorded DigDraw equal the rollout's over
  the plan's seed) feeds `ActLineHoldMask` (MTG_ACT_LINE_HOLD, the trailing-activation hold): owed while the cycler is still
  in hand; `CycleSeedPayScope` stops the seed's own payment being held against itself (rollout `cycle_chain_step`, human
  DigDraw pass, executor `PerformDigFull`). Only a provider that searches its cycles carries seeds (CycloneProvider), so every
  other deck is unchanged. Lossless (the hold is a held-first attempt with an unrestricted retry). Scenario
  `cyclone_cycle_seed_holds_its_colour`: round-4 kill; with MTG_ACT_LINE_HOLD=0 round 5. Unit `test_cyclone_refs.cpp`
  (traits lockstep plan vs record; the Song leaves an Island; the seed's own payment takes it; released once cycled; the
  whole line applied in the rollout reaches Emrakul).
- **Fix 2 -- cycling is a plan item in human play** (USER: *"Cycling should be a plan item like any other."*). Human play now
  gets the SAME cycle seeds the search enumerates (one per distinct admitted cycler; the scry and the free cast stay
  questions at resolution), so a cycle rides in one plan with the casts and land and the plan's payment covers it (Fix 1);
  `AppendHumanPlayDigPlans` no longer adds standalone twins for those cyclers (a deck that does not search its cycles keeps
  the standalone digs). CheckLine: new verbs `cycle=` / `sacdraw=` (a DigDraw is matched ONLY by them, never inside the cast
  multiset); "**" step `cycle=`. Summary: "...cast: Seething Song, Unpredictable Cyclone, cycle Curator of Mysteries
  (Unpredictable Cyclone: exile until a creature, may cast it free)". Viewer: the hand's cycle badge QUEUES the cycle into the
  line; a queued cycle ends its segment (the plan cycles after its casts; "cast A, cycle B, cast C" commits as [A, cycle B],
  [C]); on a standalone-dig deck it is a segment of its own. Scenario `cyclone_human_cycle_in_line_keeps_blue` (the human
  line, auto-paid, round-4 kill).
- **Fix 3 -- the Remand prompts say they are PAID** (USER: *"it isn't really a free cast ... It is just that we don't have to pay
  more than 2 mana for it."*). The Remand / Remand-on-Remand prompts reuse `free_cast` (same `source`, encoding and frame
  identity, so every reference replays); they now carry `paid_cost` / `paid_target` / `paid_outcome` (FreeCastPaidInfo,
  worded from card parameters -- cant_be_countered, the Remand's draw), and `freeCastPanelHtml` renders "Cast Remand on
  Emrakul, the Aeons Torn? (pay {1}{U})" with what happens, instead of the World War Hulk "cast for free" text.

Validation: unit 2097/2097, scenarios 332/332, viewer_linebuild_check 0 FAIL, viewer_protocol_check --strict 678 refs 0
play-drift / 0 board-diverged / 0 enum-gap, smoke 149/149 PASS with play-changed 0 (searched and d0) on every suite deck
(THREADS=4, MTG_PGO=0; the hold and the human seeds are inert without a cycle-searching provider). No GT accept.

Open (for the USER): QR1 -- plan-item cycling is wired for decks whose provider SEARCHES its cycles (Cyclone); on every other
deck a cycle is still a standalone plan (the search there uses the generic dig gate, the USER's 2026-09-29 rule), but the
viewer badge now QUEUES it (as its own segment) instead of firing at once -- extend plan-item cycling to every deck?
Default: Cyclone-style decks only. QR2 -- the four tied games (s1, s5, s8, s10, all T5): worth a large-budget look for a T4
line? Default: no, the USER's bar (match every reference) is met.

## Memory: M1/M3 lazy fans + M2 stored-size pricing (2026-10-28)
Design: `docs/design/cantrip-search-optimization.md` §4 (M1, M2, M3) and §6 item 7. Branch `cyclone-mem` (LOCAL), off
330cc85bf. Scratch `build/scratch/cyclone/mem/`; summaries `logs/cyclone/mem/`.

**Where the pool's bytes were (measured first, before any change).** The 8-game single-thread probe (seed 7600, d3 b10,
`--ignore-play-profile`, profile attached, pool BOUNDED at 8 GB so nothing is refused) with the new per-cache breakdown
(`MTG_PLAN_POOL_STATS=1`): the whole pool peak (1,774,286 KB) is the BREAKPOINT CONTINUATION cache; the m1 enum memo peaked
at 443 KB and the m2 memo at 0. Of the 8,942,286 plans that cache stored, 7,707,972 (86%; 89% of the bytes) were
Brainstorm / Brainsurge put-back clones and 513,768 (5.7%) Ponder ORDER clones; tutor clones 6, look-top 0. So M1 (the bp
cache) and the Ponder half of M3 are the whole of it on this deck.

**What was built (0fce7726a).**
- **M1** (`MTG_BP_LAZY_CANDS`, DEFAULT ON). `BpDeriveContinuationList` enumerates with a lazy layout and runs its three
  post-steps on POSITIONS: the new-only filter (the verdict read off a synthesised entry's prototype -- `keep()` reads only
  the actions and the land fields, which a clone shares), the `MTG_BP_CANDS_ORDER` ranking (MoveOrderLess on positions,
  the `PbLazyOrderFs` argument) and the condemnation DEMOTE stable partition; the result is a SELECTION (`CandOrder.perm`,
  possibly shorter than the full list), then COMPACTED (only the real entries and fans it names are kept; the selection
  dropped when it is the identity). `BpEnumEntry` holds `plans` + `lazy` + `ord`; every reader goes through `View()` /
  `Size()`: the node's `g_bp_cands_pb` channel (`PutBackClone` / `Shared`), the chain scan (`BpChainCandIndexView`),
  `out = cands[k]` (`CopyTo`, one copy), the canon front, the EMPTY pre-skip (real entries only -- a synthesised entry
  carries its fan's varied field, so it is never apply-empty), the verifiers (materialised). The executor's
  `EnumerateBreakpointPlans` materialises (a copy, as before); `EnumerateBreakpointPlansRef` materialises a lazy entry into
  a thread copy. The m2 memo's promotion store keeps the lazy form and hands the list back whole. The SKIP marking mode
  (rewrites entries in place) and front-only derivations stay whole. A lazy bp entry is priced at its EXACT materialised
  size (`BpLazyFullBytes`: the stored list is shrunk before pricing, so there is no slack to reproduce) -- admission,
  residency and units unchanged.
- **M3.** `PbLazyLayout` is now a list of BLOCKS of fans (each block right before a fixed real entry) instead of one block
  plus one twin block; fan kinds put-back / look-top / Ponder ORDER (`MTG_PB_LAZY_PONDER`, DEFAULT ON); a soulbond twin is
  a fan of its own (its prototype carries the twin's two edits) in a twin block at the twins' position. Look-top fans now
  coexist with a put-back block (before, the look-top fan stayed whole behind one). NOT lazy, by construction (the
  layout's note): the Ponder KEEP-vs-SHUFFLE pair (it varies `actions[ai].ponder_keep` and leaves `ponder_choice` at -1,
  so later axes -- the put-back axis among them -- clone those clones: a cross product) and the TUTOR fan (it sits before
  the Ponder and put-back axes, which clone tutor variants; 6 of 8.9M stored plans on the probe).
- **Verify** (`MTG_PB_LAZY_CANDS_VERIFY`) now also re-runs the bp post-steps on the materialised derivation and compares
  every position and the demoted list; its enumeration check compares the dig / bounce fold ids up to a consistent
  relabelling (their counters -- `NextDigGroup`, the bounce axis's -- cannot be rewound by the second run: Hinata read 92
  false MISMATCHes on 4 d5 games with the PRE-change binary too).

**Verify: 0 mismatches.** Single thread: Cyclone d3 b10 x4 (seed 7700) + d5 b20 x2 (7900) -- enumeration 18.9M entries,
bp pipeline 10.0M entries; Hinata2 d3 x8 / d5 x4 (`--ignore-play-profile`); NivMizzetCombo d3 x4 with
`MTG_NIV_PONDER_KEEP_SHUFFLE=0 MTG_NIV_BRAINSTORM_SINGLE=0` (so its Ponder ORDER and put-back fans -- and the Tandem
Lookout twins -- are lazy); LandfallElementals d3 x4 (look-top blocks).

**Byte-identity (M1 + M3, M2 off).** Pre-change binary (330cc85bf) vs the change, the same manifest (d3 b10 x80 seed 101000
`--ignore-play-profile`, d5 b20 x40 seed 102000 at the play settings), concurrently, 2 threads each, pool bounded at 20 GB
(no refusal in either: base printed no [determinism] warning, the change `refused=0`; hiwater 17.8 / 17.7 GB -- the pool
that does not bind on 2 threads is ~18 GB): **120 / 120 per-game digests, win turns and work units identical** (job digests
d3 80fd5a152c977144, d5 28986e8bd1a3992d on both). The final build (canon-front view path, verifier) re-checked on the
first 24 + 12 games: 36 / 36 identical. Smoke (THREADS=8): **149 / 149 PASS, play-changed 0** (searched and d0). Unit
2099 / 2099, scenarios 333 / 333.

| 8-game probe (seed 7600, 1 thread, pool 8 GB) | max RSS | pool hiwater | stored bp entries priced / resident |
|---|---|---|---|
| before (330cc85bf) | 2,081,396 KB | 1,774,286 KB | -- (all materialised: 15.1 GB cumulative) |
| M1 + M3 | **250,424 KB** | 1,774,286 KB (priced as before) | 14.65 GB / 0.82 GB (39,599 lazy lists) |
| M1 + M3 + M2 | **244,648 KB** | **162,514 KB** | 0.82 GB / 0.82 GB |
The identity batch: max RSS 20,103,904 KB (base) -> 918,464 KB (the change), 2 threads each, same games.

**M2: price lazy entries at their STORED size** (`MTG_PLAN_CACHE_STORED_PRICE`; the bp cache, the m1 enum memo and the m2
memo all charge `PbLazyStoredBytes` -- real entries, one prototype per fan, the pin rows, the layout's index vectors and the
selection). It changes pool ADMISSION wherever the pool binds, so it was measured as a play change. **ADOPTED, DEFAULT ON**
(the brief's bar: not worse, and it removes the warning); `=0` restores the materialised price.

(a) **Determinism (Q34).** The same manifest (d3 b10 x48 seed 101000 `--ignore-play-profile`, d5 b20 x24 seed 102000) at 6
and at 12 threads, the SHIPPED pool (unset: 2,400 MB, 24 x 100 MB), M2 on and off:
| run | refused stores | pool peak | [determinism] warning | vs the same arm at the other thread count |
|---|---|---|---|---|
| M2 off, 6 threads | 427 | 2,457,599 KB (full) | yes | -- |
| M2 off, 12 threads | 1,121 | 2,457,599 KB (full) | yes | 72 / 72 digests + units identical |
| M2 on, 6 threads | **0** | 528,845 KB | **no** | -- |
| M2 on, 12 threads | **0** | 716,906 KB | **no** | 72 / 72 digests + units identical |
All four runs are byte-identical to each other (digests, win turns, units) on this sample: the refusals were there, but on
these 72 games none moved a unit. They do move one on a larger sample -- (b)'s d3 gi79 -- and that one is exactly the Q34
mechanism: the M2-off batch read 6,315,715 units, while the same game ALONE (single thread, no refusal) reads 6,315,629 in
BOTH arms, which is what the M2-on batch read. With M2 the pool no longer binds at 6 or 12 threads at the shipped size, so a
pooled Cyclone result is reproducible across thread counts by construction (no refusal), not just on a lucky sample.

(b) **Held-out paired A/B, M2 on vs off** (fresh seeds 107000 d3 b10 x200 `--ignore-play-profile` and 108000 d5 b20 x100
at the play settings -- unused anywhere in the ledger or the Cyclone scratch; profile attached; the shipped pool; the two
arms as two processes run CONCURRENTLY, 6 threads each -- the pool is process-global, so arms that price it differently
cannot share one; Release binaries run directly, i.e. no PGO on either arm):
| depth | n | off | on | delta (se) | on slower / faster | identical digests | units on/off |
|---|---|---|---|---|---|---|---|
| d3 b10 | 200 | 5.1400 | 5.1400 | **0.000 (0.000)** | 0 / 0 | 200 / 200 | x1.0000 (302,111,591 -> 302,111,505; gi79 -86) |
| d5 b20 | 100 | 5.1200 | 5.1200 | **0.000 (0.000)** | 0 / 0 | 100 / 100 | x1.0000 (210,363,691 both) |
| | refused stores | pool peak | max RSS | process wall | summed thread-ms d3 / d5 |
| off | 4,458 (+ warning) | 2,457,599 KB | 1,135,012 KB | 2,597 s | 8,691,990 / 6,594,463 |
| on | 0 | 1,254,460 KB | 1,967,404 KB | 2,582 s (-0.6%) | 8,616,419 (-0.9%) / 6,562,236 (-0.5%) |
Peak RSS RISES (1.1 -> 2.0 GB at 6 threads) because the pool is now spent on what it really holds: entries the materialised
price refused are resident. That is the point of the change and stays far under the old 3+ GB / worker. The wall reading is
inside the noise of a box at load average ~30 (another agent's batches ran throughout) and is not claimed.

**Verdict: M2 ships DEFAULT ON** -- quality identical (0 of 300 games changed a digest or a win turn), the [determinism]
warning gone at the shipped pool at 6 and 12 threads, units identical except the one interleaving artifact it removes. Smoke
with M2 on (THREADS=8): **149 / 149 PASS, play-changed 0** (searched and d0; no smoke deck's pool binds, so M2 cannot
move one) -- no GT accept. Unit 2099 / 2099, scenarios 333 / 333. Artifacts: `logs/cyclone/mem/` (identity.txt,
determinism.txt, m2_ab.txt, probe.txt, verify.txt, both smoke logs, the manifests, cmp.py).

### Thread-count determinism by construction (2026-10-29)
USER (on the M2 result): *"We don't want any non-determinism regarding thread-count."* Then: *"it would be better to make
cache hits and misses cost the same in budgeting"*, *"we don't want to leave nondeterminism in the application"*, and the
target *"relatively neutral results in terms of performance and quality rather than a big win"*, preferably with ONE
setting. Binary lineage: 0fce7726a (M1+M3) + edaba76eb (M2) + ab4fbc6a2 (this work).

**Root cause, measured (not assumed).** The coordinator's hypothesis -- "a hit charges less than a recompute" -- was
checked first and is NOT what moved Cyclone: on the 8-game probe and on held-out d3 gi79, every one of the 126k-138k
plan-cache computations (bp derivations, m1 / m2 enumerations) charged ZERO units (a new census,
`MTG_PLAN_POOL_STATS`: "computations ... charging-units=0"). A single-thread sweep of the pool (5 MB .. 8 GB, M2 on and off)
never moved gi79 either. What the cache DOES change is what a lookup serves, and what the computation leaves behind. Five
mechanisms, each found by making the cache's residency the only variable (bp cache off, enum memo off, tiny pool) and
diffing per-decision units (`MTG_TURN_CENSUS`):
1. **Key hole: physical card copies (Cyclone).** BuildBreakpointKey folds zones by card NAME, but the search shuffle
   (`Library::ShuffleByKey`) orders the library by card NUMBER, so two states holding the same names in different copies
   shuffle -- draw, put back, fan -- differently. `MTG_BP_ENUM_LIBCHECK` on gi79: hits served under a library differing BY
   COPY ONLY with CONTENT different from a fresh derivation; gi79 read 6,316,101 units with the bp cache off and 6,315,629
   on. Fix: `MTG_PLAN_CACHE_EXACT_KEY` (DEFAULT ON) folds every zone's physical card numbers into the three pool-bound
   caches' keys. After: LIBCHECK 0 / 1.06M content diffs; cache on = off = tiny pool = 6,316,101.
2. **Key hole: the main-1 hand mask (Hinata2; PRE-EXISTING at 330cc85bf).** `GameState::HeldSinceMain1` (a per-NUMBER
   mask) bars a land held since main 1 from the main-2 drop, and no key folded the mask: `MTG_ENUM_MEMO_VERIFY` on seed
   123024 gi24 found 7 hits serving a main-2 list WITHOUT its land-drop half (cached 1 vs fresh 2 plans, state
   fingerprint equal) -- also on the pre-M1 binary. Fix: the mask is folded with the physical cards. After: 0 mismatches.
3. **Side channels of a derivation (Hinata2).** With every served list EXACTLY equal to a fresh derivation (a new
   `MTG_BP_ENUM_VERIFY=3`: Plan::operator==, fold ids up to relabelling -- 59,846 hits, 0 diffs), gi24 still read 379,907
   units with the bp cache off and 379,700 on. A derivation enumerates and the enumeration APPLIES plans on copies; those
   applies bump the channels the CALLER reads as before/after deltas around its own apply (g_bp_any_last,
   g_bp_classon_last, g_bp_latent9_last, g_bp_truncasym_last, g_look_top_resolved -- the stillborn decline, the NOBP skip,
   the uniform collapse, the slot-prefix probe) and overwrite its last-values (g_bp_seen_last / fired / cands / demoted /
   chain_ci). So the caller saw nested events only on a MISS. (Not one of eleven other memos -- solve, rollout-m1,
   exhaustive-leaf, FS no-win, LA tie-prune, no-win store, collect, pay-probe, SPS walk, arm hash, RH moved -- carried it:
   each disabled in both arms left the gap at +207.) Fix: `MTG_BP_DERIVE_ISOLATE` (DEFAULT ON): a derivation saves and
   restores those channels, so a miss is observably a hit. After: bp cache on = off = tiny = enum memo off = 379,527.
4. **The cost a hit skipped (KittyEquipment).** Kitty's derivations DO charge: the equip-route DFS runs inside the
   enumeration and bills the decision budget (8 units / link). A hit skipped that. The user's rule, built two ways:
   *mode 2* stores the units a computation charged and replays them on a hit -- MEASURED to OVERCHARGE: the route
   memoises its boards per DECISION (`SeenForThisDecision`), so a recompute is warm where the first run was cold: seed
   1015 gi14 d3 b10 read 20,443 units (and lost a turn, T3 -> T4) against 4,424 when every lookup recomputed (bp cache
   off). *Mode 1* (DEFAULT, `MTG_CACHE_HIT_CHARGE=1`) is the recompute itself: a computation that charged any unit, or ran
   a decision-memoising charging sub-search (the equip route, the AA route walk -- `TurnSolver::NoteChargingSubsearch`),
   is not stored, so every lookup of it costs what computing it costs; a computation that charged nothing is stored and
   served free, which is what a recompute costs. Plus, on every hit, the computation's side counters (group-cap drops,
   condemnation drops, truncation events, budget binds) are replayed as the enum memo already replayed most of them.
   Two more context dependences found the same way on Kitty and closed: the route's per-decision verdict memo
   (`TrialPolicy`, reached without a walk -- now noted too) and the SEARCH CONTEXT the route reads to decide whether it
   runs at all (beyond the horizon, the depth-0 runner, a decision root open, a finite budget): folded into the three
   caches' keys with the physical cards. After: seed 1015 gi14 d3 4,424 units with the cache on, off and at a tiny pool;
   d5 10,340 on and tiny (10,348 with the bp cache fully OFF, which also disables the canon-front memo -- a per-game,
   per-thread memo outside the pool, so outside this contract; noted, not chased).
5. **The GLOBAL pools.** Even with 1-4 the plan pool's admission depended on what OTHER threads held. Fix:
   `MTG_PLAN_CACHE_PER_THREAD` (DEFAULT ON): admission against a fixed per-thread quota (`MTG_PLAN_CACHE_THREAD_KB`,
   default 512 MB; with an explicit `MTG_PLAN_CACHE_KB` the quota is that budget's share per machine worker, =0 unbounded),
   the caches being thread_local and cleared per game -- so a game's admissions are a function of its own history. The FSL
   line-cache pool likewise (`MTG_FSL_PER_THREAD`, DEFAULT ON: MTG_FSL_POOL / the machine's worker count per thread; the
   process bound unchanged). The FSL cache cannot take hit-equals-miss: an entry is reused across iterative-deepening
   passes, as a truncated line or a no-win bound, so what a recompute at the hit site would return or charge is not a
   recorded quantity. (Neither Cyclone, Hinata nor Kitty touches the FSL pool at these settings: 0 MB used.) Other
   pools in MemBudget.h: the TT cap is per table, from the machine's RAM and worker count (constants), and every TT is a
   per-decision local -- deterministic per decision; no other global pool. Thread-local caches surviving a game: the
   cantrip rank cache (exact key, verify mode, no units -- wall only); every other plan memo is cleared per game
   (ClearPerGameCaches) or epoch-scoped per decision.

**Invariance, proved on the final binary (ab4fbc6a2).** One manifest, 168 games: Cyclone d3 b10 x48 (seed 121000,
`--ignore-play-profile`), Cyclone d5 b20 x24 (122000, play settings), Hinata2 d3 b10 x48 (123000), KittyEquipment d3 b10 x48
(124000); profiles attached; `MTG_DUMP_UNITS=1 MTG_SLOW_GAME_MS=1`:
| run | threads | plan pool | refused stores | vs t6: digests / win turns / units |
|---|---|---|---|---|
| t6 | 6 | shipped (512 MB / thread) | 0 | -- |
| t1 | 1 | shipped | 0 | 168 / 168 identical |
| tiny6 | 6 | 20 MB (833 KB / thread) | 382,776 | 168 / 168 identical |
| unb6 | 6 | unbounded (`MTG_PLAN_CACHE_KB=0`) | 0 | 168 / 168 identical |
| t12, tiny12 | 12 | shipped / 20 MB | -- | **PENDING** (t12 was paused by the coordinator at 161 / 168 games when the box load reached 75; tiny12 not started) |
Per-game peak plan-cache holding (the quota's question): 158 games of that manifest max 330 MB, p99 268 MB, p95 128 MB,
median 9.7 MB; the 1,300-game A/B below max 512 MB (ONE game reached the quota: 4 refused stores -- a refusal is now a
fact about that game alone and changes nothing, see tiny6), p99 218 MB, p95 100 MB, median 1.3 MB. Default quota kept at
512 MB; 1 GB would remove the one binding game at a worse worst-case RSS bound (workers x quota).
The 107000 / 108000 A/B shapes (d3 b10 x200 + d5 b20 x100) at the shipped quota, 6 threads: play identical to the
2026-10-28 M2 run of the same manifest (300 / 300 digests; units differ in 4 games, the exact key), 19 refused stores
(several heavy Cyclone games reach the 512 MB quota: per-game peak p99 524 MB, p95 192 MB, median 43 MB -- harmless now,
see tiny6). The same shapes at 12 threads: **PENDING** (box over the thread cap).

**The play change, measured.** Smoke (THREADS=5): **149 / 149, play-changed 0** on every deck. Held-out paired A/B, the
committed tree (edaba76eb) vs this change, ONE manifest run as two concurrent 5-thread processes (fresh seeds 140000-145000,
unused in the ledger and the Cyclone scratch), profiles attached:
| job | n | old | new | delta (se) | new slower / faster | digests identical | units new/old | thread-ms new/old |
|---|---|---|---|---|---|---|---|---|
| Cyclone d3 b10 (140000) | 200 | 5.080 | 5.080 | 0.000 (0) | 0 / 0 | 200 / 200 | x1.0000 | x1.005 |
| Cyclone d5 b20 (141000, play) | 100 | 5.230 | 5.230 | 0.000 (0) | 0 / 0 | 100 / 100 | x1.0000 | x0.996 |
| Hinata2 play (142000) | 200 | 5.300 | 5.300 | 0.000 (0) | 0 / 0 | 200 / 200 | x1.0000 | x0.971 |
| Hinata2 d3 b10 (143000) | 200 | 5.420 | 5.420 | 0.000 (0) | 0 / 0 | 200 / 200 | x0.9999 | x1.008 |
| KittyEquipment d3 b10 (144000) | 300 | 3.643 | 3.643 | 0.000 (0) | 0 / 0 | 300 / 300 | x1.0099 | **x1.269** |
| KittyEquipment d5 b20 (145000) | 300 | 3.740 | 3.740 | 0.000 (0) | 0 / 0 | 300 / 300 | x1.0047 | **x1.130** |
(Win turns from the per-game `.wins`, unwon = 9; process wall 3,977 s -> 4,007 s, +0.8%; max RSS 1.56 -> 1.64 GB.) **No slower
game anywhere, so there was nothing to root-cause; quality is identical on all 1,300 games.** Units move only where a hit used
to skip a charge: Kitty +0.5% / +1.0% (some game's decisions now pay its route recomputes; no decision changed), Hinata
-0.01% (the isolated channels). Wall: neutral on Cyclone and Hinata; **Kitty +13% / +27% thread-time** -- its route-charging
derivations are recomputed on every lookup now (mode 1), 56k charging computations and 561k unstorable ones in the run.
**Budget factor: 1.0 (none).** One global factor cannot buy back the Kitty wall without changing play (a factor changes
how much is searched, and play is already identical), and no deck lost quality to re-gain. The Kitty wall is the price of
exact hit-equals-miss on a deck whose derivations run a decision-memoised sub-search; the alternative that would recover
it -- caching those derivations per decision only (epoch-scoped) at the cost the route's warm memo implies -- is a design
question for the USER (Q57; Q54 on cyclone-mem), not a settings one.
Regression tier: not run (my thread share; the A/B above is 1,300 games on the three decks the change can move, and
smoke covers every deck).

## Open questions for the user (collected, never blocking)
- Q1. Win-turn metric with extra turns -- ANSWERED 2026-10-26: extra turns do not count (rounds).
- Q2. Shock lands always paid untapped while life > 2 (not searched) -- ANSWERED 2026-10-27: leave as is
  (USER: *"I don't think life matters in the deck all that much"*). Toxic Deluge pruned (goldfish_inert).
- Q3. Searched take/decline free cast: Cyclone only, or also cascade / Creative Technique / Breaching
  Dragonstorm? Default: Cyclone only.
- Q4. A Cyclone walk opens a mid-turn re-solve (new information)? Default: yes.
- Q5. Cyclone's bottom order is truly random (cascade keeps its fixed walk order; changing it would move
  those decks' GT)? Default: random for Cyclone only.
- Q6. Remand on a free cast -- ANSWERED 2026-10-27: USER *"It is actually playable in this deck, but only
  off cyclone triggers themselves."* So PHASE 3 BUILDS Remand on a Cyclone free cast (the walk's hit goes on
  the stack; Remand it -> it returns to hand + Remand draws; e.g. rebuy a Street Wraith / Curator and cycle
  it again). Searched (a plan/continuation option on the free cast). Remand on a free-cast EMRAKUL is
  ALSO offered (USER 2026-10-27: *"countering Emrakul with remand is okay since it just draws a card"*):
  Emrakul can't be countered, so it resolves normally and Remand is a {1}{U} cantrip -- the existing
  `cant_be_countered` Remand path already models exactly that. Remand on a free-cast ULAMOG (either) is
  PRUNED in CycloneProvider (USER 2026-10-27: *"I would probably heuristically prune remanding either
  Ulamog"* -- they CAN be countered, so Remand would bounce the threat to hand). Implement as a provider
  restriction hook (generic hook, Cyclone override), not a card-name check in the engine.
  HAND-CAST self-Remand: USER 2026-10-27 *"We may want to try allowing remands on other cheaper spells.
  (say on something MV 1) It isn't great, but sometimes replacing the card may be good enough, kind of like
  a cycle."* Economics (USER correction 2026-10-27): the line costs {1}{U}{U} -- the MV-1 spell's {U} is
  paid too and it does NOT resolve (it returns to hand) -- so in effect Remand is cycled for {1}{U}{U}: Remand
  is replaced by a fresh card, the 1-drop is back where it started. So the hand-cast prune is an MV CEILING, not a blanket ban: CycloneProvider admits a
  hand-cast self-Remand only when the target's MV <= `MTG_CYCLONE_REMAND_MAX_MV` (provider lever,
  per-job via HeuristicArm so arms pool in one batch). Shipped default 1 (the wider, searched side);
  Stage 5 A/B: ceiling 1 vs ceiling -1 (no hand-cast Remand), paired, held-out seeds -> result to the USER,
  who decides. (An earlier draft of this entry said hand-cast self-Remand was pruned outright; that reading
  of "only off cyclone triggers" is SUPERSEDED by the MV ceiling above.)
- Q7. APPROVED 2026-10-27: USER *"All spells are cast at sorcery speed here so that is probably fine."* Original: Instant timing collapsed to main phase for Brainsurge / Long-Term Plans / Curator & Wraith cycling
  (incl. the opponent's-turn cast after our cleanup keeping an extra card). PROVISIONAL.
- Q9. An extra turn costs no search ply (depth counts rounds). Default: yes.
- Q10. Eldrazi Conscription's annihilator note (2026-10-05 approval) rests on a false premise: the
  goldfish opponent does get spawned tokens in 8/10 games. Re-examine that card. Default: flag only (out of
  this deck's scope).
- Q11. Ulamog CH's attack trigger "defending player exiles the top twenty" -- PROVISIONAL inert (damage
  kills before the exile can ever matter: 10 power x 2 attacks = 20; 2HG 30 life dies on the 3rd attack's
  damage in the same combat).
- Q12. Emrakul's "protection from spells that are one or more colors" -- PROVISIONAL inert vs the passive
  opponent (our own colored spells never target it in this list).
- Q13. Extra-turn decks lose the turn-edge prunes (they assume the next turn is past the cutoff) -- a
  perf cost for this deck only; a sound refinement later. Default: off for extra-turn decks.
- Q8. CycloneProvider TutorSearchWidth 32 (coverage fix: AL's width 2 makes Emrakul unreachable for
  Long-Term Plans). Default: yes.
- Q14. **FIXED 2026-10-27 (phase 4b, WhiffShuffle -- see the Phase 4 section).** ANSWERED 2026-10-27: USER *"Scalding Tarn sacrificing and finding nothing should indeed allow for a shuffle."* -> FIX QUEUED LAST, after phases 2-4 (USER: "can happen after everything else ... It isn't a key change"; generic engine fix in PerformFetch; may move other decks whose fetch can whiff -- measure in smoke/regression as a play change). Holding / late-cracking a fetch stays DEFERRED (USER: "isn't often super relevant"), recorded in docs/design/uncracked-fetchland.md. Original question:
  "then shuffle" (PerformFetch returns early). Rules-wrong but rare; fixing it moves any deck whose fetch
  whiffs. Default: disclosed, not changed (phase 1).
- Q15. APPROVED 2026-10-27: USER *"Swampwalk is fine to treat as inert until 1v1 play."* Original: Street Wraith swampwalk (+ Landwalk tag) inert vs the never-blocking goldfish -- PROVISIONAL deferral.
- Q16. APPROVED 2026-10-27: USER *"Annihilator like that is fine."* Original: Annihilator: the passive opponent sacrifices its OLDEST permanents first (a fixed rule for the
  opponent's choice; every spawn is an equally good Ulamog target). Default: oldest first.
- Q17. APPROVED 2026-10-27: USER *"Fortunately the targets don't matter much for ulamog in goldfish so I'm sure that is fine."* Original: Two targeted-cast-trigger casts in one turn (Ulamog CH + Gyre) share ONE searched rank policy (the
  landfall_target_choice whole-turn shape), so their target choices are not searched independently.
  Default: accepted (both in one turn needs 21 mana or two Cyclone hits); flagged for phase 3.
- Q18. The turn_number classification used an exhaustive grep of all 954 reads instead of a compile-break
  rename (the identifier has no macro/token-pasting use, so the two list the same sites). Default: grep
  accepted; a rename pass can be run on request.
- Q19. HOW THE SEARCH TREATS AN EXTRA TURN (USER 2026-10-27: *"We'll have to decide how best to treat extra turns in terms
  of searches, but the top thing is that they are not counted negatively in our search optimization."* and *"My guess is
  that they could be treated like new phases (or equivalent)."*). The top requirement is MET and verified (orchestrator,
  2026-10-27): every search/executor win return reports `state.turn_number`, the ROUND (GameEngine.cpp:156,
  TurnSolver.cpp ~80783-81019 and ~94369-94383), and no scoring site reads TurnSeq(), so an extra turn never costs win turn
  in the objective. Depth counts rounds (phase 2: the ply is given back after an extra-turn transition). OPEN: the search
  STRUCTURE. Today an extra turn is a real turn transition (a new turn's solve, turn-edge prunes off for the deck: Q13).
  USER's suggestion: treat the extra turn like further phases of the same turn (or equivalent), i.e. one search frame
  continuing through it, so the turn-complete / per-turn budget accounting does not split the turn the extra turn
  belongs to. USER 2026-10-27: *"But we might find this is a bad idea from a correctness standpoint. I think we should
  evaluate both ideas."* So EVALUATE BOTH, side by side, AFTER phase 4 (with the Stage 5 measurements):
  (A) extra turn = real turn transition (as built in phase 2); (B) extra turn = more phases of the same search frame.
  First CORRECTNESS: (B) must still do everything a turn boundary does (untap, upkeep triggers, draw, land drop reset,
  summoning sickness cleared, until-end-of-turn effects expire, 'this turn' counters/stamps reset, a second combat with
  annihilator, cleanup discard) in executor AND rollout, and must not let the search see information it would not have
  (the extra turn's draw is unknown when the current turn is planned -- a frame that plans across it must not be
  clairvoyant on that draw). USER 2026-10-27: *"When the search is clarivoyant that isn't an issue, but you are right
  that we need to be a bit careful not to break non-clairvoyant search."* So the draw-information check applies to the
  NON-clairvoyant search modes only; a clairvoyant search may see the extra turn's draw. Then COST and QUALITY: paired A/B on held-out seeds, budgeted plus the unbudgeted backstop.
  Result to the USER, who decides.

- Q20. THE SEARCHED CYCLE FOR THE OTHER CYCLING DECKS (phase 3). The mechanism is generic
  (DecisionProvider::SearchedCycleSources / CyclesAreSearched), but only CycloneProvider opts in; the phase brief
  expected the other cycling decks to move. They do not (default taken: not opted in), because (a) the generic
  "cycle only when nothing is castable" gate is the USER's own 2026-09-29 rule, a provider RESTRICTION the no-greedy
  rule allows, so removing it is not an agent's call; (b) an opted-in deck re-solves after EVERY cycle, which would
  reshape treasure_hunt's dig-through-lands chain and Fluctuator's 16-cycler combo chain (one searched breakpoint per
  apply, canon defaults for the rest) -- a large play and cost change to measure, not to assume. What remains for those
  decks is the legacy loop's SelectDigSource pick when two or more DISTINCT sources are payable at one step (FiveColour's
  two Triomes, treasure_hunt's three cycling lands + Fiery Islet, Fluctuator's many cyclers; EDF and WhiteKnights hold one
  cycler name each, so nothing to choose) -- a residual in-window pick, disclosed. Recommendation: opt them in one deck at
  a time as a measured play change (smoke + held-out A/B), FiveColour first.
  **USER RULING 2026-10-27:** *"Not searching but just choosing one is okay iff it is a provider heuristic that is
  shown to do well against searched."* So the pick may stay for FiveColour / treasure_hunt / Fluctuator, but only as a
  PROVIDER heuristic (SelectDigSource in the deck's provider, not a generic first-affordable default), and only once a
  paired held-out A/B shows it doing well against the searched cycle (the deck opted in via CyclesAreSearched). Owed
  after Cyclone, one deck at a time: build the opt-in arm, A/B searched vs the provider pick (quality AND cost), report
  to the USER; where the pick does not hold up, opt the deck in (a measured play change).
- Q21. Curator's scry on a NON-cycle discard (the cleanup discard) uses the provider's ScryKeepOnTop in-window (the
  cycle's scry is searched; this one is not: it happens at cleanup, outside the plan). Rare (more than seven cards at
  cleanup with a Curator out). And when an Emrakul is discarded in the same cleanup, the scry resolves before the
  graveyard shuffle (a legal trigger order, not the better one). Default: accepted, disclosed.
- Q22. Remand on a Cyclone free cast in HUMAN play is a second `free_cast` prompt (candidate = the Remand in hand,
  zone "hand"), not a new decision type. Default: the reuse.
- Q23. MTG_CYCLONE_REMAND_MAX_MV is an INT, so its per-job override lives in the numeric arm
  (valuearm::Arm::cyclone_remand_max_mv, manifest key "cyclone_remand_max_mv"), not in the boolean HeuristicArm the
  brief named. Default: the numeric arm (the bp_chain_slot precedent).
- Q24. Curator's searched j counts the Curators in HAND as well as on the battlefield (a Curator the plan casts before
  the cycle triggers too), so a j above the realised trigger count is a duplicate line, never a missing one. Default:
  completeness over width.
- Q25. The UNCOMMITTED executor path (the develop-when-stuck fallback with no recorded script) realises the plan's first
  cycle seed only, and at depth > 0 does not re-solve the rest of that main. The committed line (the normal path)
  replays the whole recorded chain. Default: disclosed.
- Q26. THE DECK'S SEARCH COST (phase 3 measurement, see the Phase 3 COST paragraph): ~12.7 s/game single-thread at d3
  b10 (Hinata2 0.93 s/game; the 3x suite-entry rule would block it) and ~3 GB resident per process; the searched
  cycle is 1.71x of that against the legacy greedy cycle. Not root-caused. Candidate SOUND collapses, none built:
  (a) a seed-only plan with a replacer out has an exactly predictable walk, so its decline / take-and-Remand / j
  variants whose outcome equals another's are duplicates and can be folded; (b) the per-step site-4 re-solve's
  continuation list carries every seed variant again. Default: report; the suite-entry cost work is Stage 4's.
- Q27. Long-Term Plans in HUMAN play: a -1 at its resolution takes the default card instead of failing to find
  ("search your library for a card" names no quality, so CR 701.19b's fail-to-find does not apply). Gamble, the
  other quality-less tutor, keeps its recorded -1 = fail-to-find semantics so existing references replay unchanged.
  Default: LTP only.
- Q28. The fail-to-find shuffle (phase 4b) also covers a CHOSEN fail-to-find: the searched `kTutorDeclineTarget`
  variant of an X-capped put tutor (Chord of Calling / Green Sun's Zenith) now shuffles, since the library was
  searched. NOT changed: a "you MAY search" with nothing to find (the Harbingers, Forerunner: treated as a decline, no
  search, no shuffle) and Light-Paws' optional aura search -- both legal lines. Default: as built (rules); flag if the
  USER reads "may" differently.
- Q29. Scalding Tarn tie order past the Vents (phase 5): not short on red -> Watery Grave, Island, then Blood Crypt,
  Mountain; short -> Blood Crypt, Mountain, then Watery Grave, Island ("then blue (Watery Grave, then Island)" read as the
  specific statement over "basics only once no shock is left"). Xander's Lounge is searched beside the pick whatever it is.
  Default: as built.
- Q30. The shared red count's agent readings: at most ONE red land from hand counts (one land drop a turn); a fetchland in
  hand is not a red land; Cascade Bluffs counts TWO red beside another land that makes U or R (it filters {U/R} into
  {R}{R}) and none alone. Default: as built.
- Q31. LTP's Street Wraith target is "the cheapest creature cycler left" (Street Wraith; Curator of Mysteries once no
  Wraith is left), "no cycler in hand" = no CREATURE with cycling in hand (Cyclone's and the Lounge's cycling do not count),
  and the Seething Song lever also requires no Song in hand ("missing"). Default: as built.
- Q32 RULING (USER 2026-10-27): *"Let's leave the mulligan stuff until the profile generation. I want to focus on other
  performance wins."* The bottoming-rollout cost is parked until the mulligan-profile stage; the next performance work
  is everything else, starting with the cantrips (USER: *"We should also look at optimizing brainstorm, ponder and
  preordain (and brainsurge)"*). The suite gate still counts the bottoming cost, so suite entry will need the keep table
  (or a USER waiver) -- re-raise at that point.
- Q32. **THE 3x GATE FAILS: 17.36x Hinata** (cyclone 49,009 ms/game at d5 b20 s3003 vs hinata 2,823, same run), and it
  cannot pass with the shipped (USER-sanctioned) set, because ~80% of the deck's work is MULLIGAN BOTTOMING: with no keep
  table the default keep policy (curve check TwoDrop -- it counts neither Street Wraith's free cycle nor Curator's {U}
  cycle as a play) mulligans 35-48% of games, and each mulligan plays every candidate bottom as a full game at play
  settings (7-8x a kept game). Options, all the USER's: (a) let the mulligan profile run BEFORE suite entry
  (MTG_ALLOW_UNTESTED_DECK=1, a USER-only override) -- the keep table replaces the bottoming trials with a lookup;
  (b) adopt the put-back SINGLE pick (Q33), which is ~4x cheaper AND better; (c) a provisional profile with cheaper
  bottoming rollouts (bottom_eval_depth / budget) -- a mulligan setting; (d) a deck keep policy. Default taken: the
  provisional suite cases are left UNCOMMITTED in the worktree; the deck is not added. What-if gates (env, nothing
  adopted) in the Phase 5 section.
- Q33. **The put-back SINGLE pick (MTG_CYCLONE_PUTBACK_RULE + MTG_CYCLONE_PUTBACK_SINGLE) measures BETTER and ~4x cheaper**
  (-0.105 turns d3, -0.110 d5 vs the shipped set; -0.225 / -0.220 vs the pre-phase-5 tree; units x0.27 / x0.25; memory
  3.27 GB -> 123 MB per worker). It is a LOSSY prune (the fan is not searched), so it ships OFF: adopt? The rule as built:
  put back dead cards (goldfish_inert) first, then the Eldrazi (Ulamogs before Emrakul, so an Emrakul put back lands on top)
  while a Cyclone is out or in hand, then lands beyond the one kept, then Remand, then the deck's shed order. Recommended:
  adopt (with the put-back RULE), and consider the Ponder keep-vs-shuffle proposal neutral (+0.01, x0.98: not worth it on
  its own). Default: OFF until the USER answers.
- Q34. The plan-cache pool BINDS on this deck even at 11.7 GB on 24 threads, so its pooled results are not byte-identical
  across thread counts / interleavings (the batch's [determinism] WARNING). **RESOLVED 2026-10-29 BY CONSTRUCTION**
  ("Thread-count determinism by construction"): the plan caches now serve exactly what a recompute would (exact keys,
  isolated derivation channels) at exactly its cost (hit-equals-miss, mode 1), and the pools admit per thread. Proved on a
  168-game Cyclone / Hinata / Kitty manifest: 1 and 6 threads, a 20 MB pool (382,776 refusals) and an unbounded pool --
  all byte-identical (digests, win turns, units), and 1 / 6 threads with tiny and unbounded pools are proved; the 12-thread
  runs (t12, tiny12, the 107000 / 108000 shapes at 12) are PENDING -- Q34 closes when they pass. The 2026-10-28 M2 fix alone was not enough (USER: *"We don't
  want any non-determinism regarding thread-count"*): it only stopped the pool binding at the shipped size.
- Q35. LTP measured neutral (+0.005, se 0.037) and 8% cheaper; its 11 losses are shuffle luck by the salt ensemble (still
  slower in 1 of 11 under four other search shuffles). The Tarn rule is the only USER rule with a clear gain (-0.06 turns);
  5 of its 18 losses persist across salts (gi 25, 61, 130, 154, 184 of seed 81000 d3) -- root-cause them if the rule is to
  be refined. Myriad neutral. Ulamog / Song amendments inert or neutral in this sample. Default: all as built (USER rules
  ON, amendments OFF).
- Q36. Q26 (a) (fold the decline / take-and-Remand / j variants of a seed-only plan whose Cyclone walk is EXACTLY
  predictable) is not built: the whole seed-pin axis is 2.7% of the work. Owed as a sound collapse. Default: deferred.
  **UPDATE 2026-10-28: BUILT** (`MTG_CYCLE_SEED_WALK_FOLD`, default ON; section "Sound folds F2/F4/F5/F3 + Q36"): a
  seed-only variant whose scry + walk + keyed bottom leaves an earlier sibling's state (same pin) or whose walk whiffs
  is not emitted; verify 1,237 / 0 mismatches; 116K variants folded in the A/B. One conservatism left: a DECLINE pin
  on a castable hit is folded only when both its branches agree (the provider's take-dominance call is not read at
  enumeration).
- Q37. PHANTOM RITUAL PLANS IN THE HUMAN-PLAY MENU (Stage 5j). With one Mountain out, the viewer offers "cast: Seething
  Song" (and on turn 3 "Song, Song, Unpredictable Cyclone"); the apply drops the cast (`dropped_casts`) and the frame
  re-prompts the same menu. Cause: TurnSolver's order-free ritual affordability credit (a ritual's gross float counts toward
  its own cost), which is load-bearing as a PROPOSAL in the autonomous search (the re-sim verifies), but in human play the
  proposal is shown unverified. Generic, not Cyclone-specific (not re-checked on another ritual deck). Options: (a) verify
  each human-menu plan by an apply on a copy and hide or mark the plans whose casts drop (human-play only, GT-neutral; menu
  indices shift, so saved references replay as `repaired`); (b) leave it: the dropped-casts banner already tells the
  player. Default: (b), no engine change; play_invariants' driver now passes on such a menu (1b18e71e8).
- Q38. `scripts/play_invariants.py` drives every game-index of a seed with the same `--seed`, and claude-play keys the
  shuffle on the seed, so verify_deck's "4 games per seed" are one game four times (2 distinct games, not 8). The fix is
  `--seed seed+gi` (the audit_viewer_decisions.py convention); it changes which games EVERY deck's gate drives and may surface
  new findings elsewhere. Default: not changed here (a shared tool; flagged).
- Q39. Stage 5c2 (horizon-honest tie-break): 240 paired games, net -3 turns, but the halves disagree (+6 / -9). The
  script's decisive sample (12 x 1000 per arm) is ~12 h of the box at today's cost. Default: keep the tie-break ON (the
  script's verdict); re-run at size once Q32 / Q33 make the deck cheaper.
- Q40. Budget starvation at the shipped play settings (d5 b20) and the d3 b10 gate cell: 7 of the 14 slow games re-run gain
  one turn at 5x-20x budget; none of the unwon games is budget (mulligan / mana screw). Play depth and budget are the USER's.
  Default: no change; the Q33 single pick (~4x cheaper per game) is the lever that relieves it without a budget change.
- Q41. The cost gate's timing depends on HOST load: the same units measured 17.36x (phase 5) and 10.49x (2026-10-27, host
  loadavg ~41-45 from another container; Hinata's ms per unit 2.2x the phase-5 run, cyclone's 1.3-2.1x). Default: report
  both; the FAIL holds either way; re-time on a quiet host before trusting a verdict near the 3x limit (the Q33 what-if,
  3.93x, is such a case).
- Q42. THE ANALYZER'S DISCARD ANALYSIS finished (Stage 4, report only): RULE_FOUND. Over 125 search-labelled cleanup
  discards (400 games d3; 97% have several optimal picks), a derived order beats the current ranking: optimal 92.0% vs
  80.8%, mean regret 0.096 vs 0.216. The proposed shed order (most expendable first): Steam Vents, Toxic Deluge, Cascade
  Bluffs, Remand, Ulamog the Ceaseless Hunger, Myriad Landscape, Xander's Lounge, Ulamog the Infinite Gyre (then the
  deck's default). Most costly to discard: Street Wraith, Unpredictable Cyclone, Seething Song, Scalding Tarn, Ponder.
  Paired outcome A/B: d0 -0.0285 turns (n 4000, t -2.37), d3 -0.0225 (n 800, t -0.81, not significant). Discard
  buckets are the USER's: the order is NOT adopted. Note that Steam Vents first reads oddly beside the USER's Tarn rule
  (Vents is the fetch's top pick); it was offered only 9 times. Default: not adopted; it would become
  CycloneProvider::CleanupDiscardCandidates on approval. Report: build/scratch/cyclone/stage4.json (discard_analysis).
- Q43. CANTRIP ITEM 1 SHIPPED ON (an ORDER, as the design and brief direct): `MTG_CYCLONE_CANTRIP_ORDER` measures d3 -0.100
  (se 0.053), d5 -0.080 (se 0.081), kept-game units x0.93 vs the order off, fresh seeds 101000 / 102000. Two defaults
  were taken on the USER's open points: **a missing piece means k >= 2** (built as k = 3, which turns off the walk terms and
  admits the shuffle line), and **for k = 1 with two Eldrazi only one is placed for the walk** (the other stays in hand or
  rides position 1 and is drawn back). Confirm both. Default: as built.
- Q44. Two scorer amendments from the root-causes (Cantrip item 1 section): (1) a put-back card that cannot be CAST before
  its redraw costs ~0 to put back (the Niv "can't use yet" rule; d5 gi13 kept two uncastable Brainsurges and put back two
  Remands); (2) the cleanup overflow -- the deck's generic shed order sheds the sole Unpredictable Cyclone (d5 gi5; recovers
  at d8 b0), best fixed by a Cyclone cleanup discard that never sheds it (Q42's derived order). Default: neither built.
  **UPDATED 2026-10-28 (section "Cantrip scorer: item 8 cache + Q44(1)"):** (1) BUILT as `MTG_CYCLONE_PUTBACK_USABLE`
  and measured on fresh held-out seeds (103000 d3 b10 x400, 104000 d5 b20 x200): on - off **d3 +0.015 (se 0.027), d5
  +0.045 (se 0.039)**, units x1.000 / x1.015 -- not better, so it ships **DEFAULT OFF** (the agent's call on an
  agent-proposed fix; veto if you want it on). It does fix gi13 (T7 -> T5). Mechanism: charging 0 flattens the early
  put-back fan to ties broken by the generic shed order (it puts back the sole Cyclone or a Curator on top). Refinement
  proposed in Q52. (2) not touched (USER-owned, tied to Q42).
- Q45. The put-back game-log record (a DRAW for each drawn-and-put-back card + a "<card> (put back)" REVEAL) is log only and
  NOT folded into the play digest, so no GT moved (smoke play-changed 0). Folding it would make the digest see a put-back
  change directly, at the price of a NivMizzetCombo GT rebaseline (its Brainstorm games). Default: not folded.
- Q46. The scored order costs 8.9% of wall (4.07 us per resolution, re-derived at every apply of every fan member); units
  and play are unaffected. Plan item 8's per-fan ranking cache removes it. Default: left to item 8.
  **DONE 2026-10-28 (item 8 built, byte-identical: 240 / 240 games' digests and units equal vs the pre-change binary,
  VERIFY 8.5M hits 0 mismatches).** Hit rate 91.8%; the scorer's own wall 14.57 s -> 8.52 s on the Q46 probe (9.9% ->
  6.1% of the process; process -5.2%); pooled d3 + d5 batch thread-ms -3.9%, process wall -2.9% (noisy box). The
  residual 1.5 us per resolution is the key build and, for Ponder, the base list built before the lookup. Not built:
  the design row's prefix snapshot. Default: shipped (on; `MTG_CANTRIP_RANK_CACHE=0` turns it off).
- Q33 (updated, cantrip item 1): SINGLE under the REBUILT rule (rank 0 of the new score) vs the shipped order: d3 -0.135
  (se 0.055), d5 -0.020 (se 0.062), units x0.22; vs the order off -0.235 / -0.100. Still OFF: the USER's decision.
- Q47. THE REMAND CEILING (2026-10-28 section). Raw paired deltas vs the shipped 1: ceiling 2 (+ Remand-on-Remand) d3
  +0.0075 / d5 +0.010, ceiling 3 +0.015 / +0.035 (se 0.015), 3 + Deluge +0.010 / +0.020, -1 -0.0125 / -0.010; units
  x1.02-1.06 (wider) and x0.92-0.95 (-1). Every faster game's gain vanishes under blind replay (search salts and real
  futures), for the wider arms and for -1 alike. Retain the lines? Recommended: no -- keep 1, chain and Deluge switch off
  (both stay in the code, gated, reachable as arms). Default: shipped 1 unchanged.
- Q48. Remand-on-Remand and a Remand-only inert target are DEFAULT-OFF for every other Remand deck (Hinata2,
  NivMizzetCombo) via the generic provider hooks, so their play is untouched; opting one in is its own A/B. Default: off.
- Q49. The chain's modelling restriction: X may open a breakpoint of its own only as a plain cantrip (Ponder, Brainstorm,
  Brainsurge); a chain on another draw class (tutor-to-hand, impulse, cascade, Expressive Iteration) is not offered (two
  breakpoints in one cast). No Cyclone card is in that set. Default: disclosed gap.
- Q50. A chain on a CAN'T-BE-COUNTERED X (a free-cast Emrakul; taken once per d5 arm) is the single Remand plus {1}{U} and
  one more cast: dominated wherever nothing reads the cast count (Cyclone has no storm / prowess / cast-draw). A sound
  deck-level fold candidate (CycloneProvider), not built. Default: not folded.
- Q51. The blind replay re-ran only the FASTER games (as briefed), so the blind deltas in Q47 are an upper bound on harm,
  and it blinds only POST-SHUFFLE knowledge (the search still sees the unshuffled library top in every arm). A symmetric
  replay of the slower games would make the blind estimate two-sided. Default: not run (the decision does not hinge on it).
- Q52. Q44(1) REFINEMENT (not built): charge a put-back card that cannot be used before its redraw a small residual
  (e.g. need / 16) instead of 0, so unusable cards still go back before usable ones but keep the need ordering among
  themselves -- the 0 charge made rank 0 the generic shed order's tie-break (it put the sole Cyclone / a Curator on top),
  which is where the held-out loss came from. Pairs naturally with Q44(2) / Q42 (a sane Cyclone shed order fixes the
  tie-break too). Measuring it is one more pooled A/B (~1 h at 14 threads). Default: not built.
- Q53. THE NUMBER-0 CARD IDENTITY DEFECT (found by the sound-fold verify runs, 2026-10-28; pre-existing, NOT fixed). The
  rollout files a resolved instant / sorcery as its definition's TEMPLATE card (apply_one,
  `ap.graveyard.push_back(UnkickedSpellCard(def, def.card))`: m_number 0), not the cast copy -- the executor files
  the real card. An Eldrazi's graveyard shuffle (`put_into_graveyard_shuffles_graveyard`) deals those number-0 cards
  back into the library and the hand, and then (a) apply_one re-finds a cast copy BY NUMBER after its payment
  (`cast_zone_number`), so casting a number-0 Brainstorm erased a number-0 Ponder (d5 b20 seed 108000, a T8
  rollout); (b) Library::ShuffleByKey ties every number-0 card and keeps their input order (std::sort over ties),
  so the search's post-shuffle library is not the one the CRN shuffle promises. Both make the clairvoyant search
  predict a hand / library the real game does not deal, after the first graveyard shuffle of a game. Proposed fix:
  file the cast copy (cast_number), the Green Sun's Zenith precedent (GszShuffleCopyIdOn) -- a play change for every
  deck that brings a graveyard back (here: Emrakul, Ulamog the Infinite Gyre). The folds that rely on card identity
  (F2, F3, Q36) require distinct numbers meanwhile. Default: reported, not fixed (outside the folds' brief).
- Q54. F4, THE STRANDED-CAST FOLD (MTG_PB_STRAND_FOLD), is built and measured NOT exact: 613 of ~297K verified pairs
  realise a different line than the smaller plan's same-pair member, because the payment allocation is steered by
  the plan's whole bill (the stranded cast's included) and a different allocation changes what the rest of the line
  can pay. Adopt it anyway as a payment-allocation fold (the land-position prefix probe's argument), or leave it
  off? Measured as an arm of the 2026-10-28 A/B (see the section). Default: OFF (not exact => not a sound fold).
- Q55. F3 is built PLAN-level only. The node-level form -- a breakpoint continuation whose first library event is a
  shuffle, i.e. the USER's "Brainstorm, then crack Scalding Tarn" line -- needs the continuation matched by identity
  across two parents' lists; not built. And Long-Term Plans (finds any card) qualifies for F3 only when its target
  is name-bound, because an open tutor's candidate list is in library order. Build the node-level form? Default:
  deferred (the plan-level fold already finds mirror-eligible ranks in ~1.8% of the fan tables).
- Q56. (cyclone-mem's Q53, renumbered at integration) M2 (stored-size pricing, `MTG_PLAN_CACHE_STORED_PRICE`) is ADOPTED DEFAULT ON (agent's call under the brief's bar:
  0 of 300 held-out games changed, the [determinism] warning gone at the shipped pool). Since 2026-10-29 it no longer
  carries the determinism (per-thread admission and hit-equals-miss do, by construction); it now only decides how much the
  per-thread quota holds. Peak RSS rises where the pool used to bind (Cyclone 6 threads 1.1 -> 2.0 GB). Veto? Default: ON.
  Also disclosed, not changed: under `=0` the m1 / m2 enum memos price a lazy list at PbLazyListBytes, which does not
  reproduce the materialised vector's growth slack.
- Q57. (cyclone-mem's Q54, renumbered at integration) Hit-equals-miss on KittyEquipment costs WALL, not quality: its continuation derivations run the equip-route DFS
  (which bills the decision budget and memoises its boards per decision), and an exact hit price is a recompute, so those
  derivations are no longer cached -- +13% (d5) / +27% (d3) thread-time on 600 held-out games, every game's play identical.
  The literal "store the units and charge them again" (`MTG_CACHE_HIT_CHARGE=2`) is built but OVERCHARGES (the recompute is
  warm), losing quality (seed 1015 gi14 T3 -> T4). A wall-recovering option, NOT built: keep such entries for the rest of
  the DECISION only (the route's memo lives that long), charging nothing on a same-decision hit -- exact only if the
  route's memo makes every same-decision recompute free. The argument holds for the equip route: every in-search route walk
  goes through `equiproute::TrialPolicy`, whose verdict memo is per DECISION and only grows (cleared only at a new decision
  or past 2^20 boards), so once a derivation has run its trials a recompute later in the same decision is a memo hit per
  trial -- 0 units and the same verdicts. So YES, such a derivation (including one whose route charged 0) is eligible to be
  stored exactly, for the rest of its decision, while the memo generation stands. Built as a patch, NOT built-tested or
  measured (the box was over the thread cap): `logs/cyclone/mem/route_decision_scoped.UNTESTED.patch`
  (TurnSolver::TrialRouteBegin/End, Cost::decision_only + route memo generation). Default: mode 1 as shipped.
  Budget factor: 1.0 (no deck lost quality; a factor cannot buy back wall without changing play).
- QR1. (Reference games 2026-10-28) Plan-item cycling in the viewer is wired for decks whose provider SEARCHES its
  cycles (Cyclone); elsewhere a cycle stays a standalone plan, but the viewer badge now QUEUES it as its own segment instead
  of firing at once. Extend plan-item cycling to every deck? Default: Cyclone-style decks only.
- QR2. The four tied reference games (s1, s5, s8, s10, T5): a large-budget hunt for a T4 line? Default: no (bar met).

## Approved deferrals
- Instant timing collapsed to main phase (Brainsurge, Long-Term Plans, Curator / Wraith cycling): USER-approved 2026-10-27 ("All spells are cast at sorcery speed here").
- Street Wraith swampwalk (and its Landwalk tag): inert vs the never-blocking goldfish opponent. USER-approved 2026-10-27 ("fine to treat as inert until 1v1 play").
- Holding a fetchland uncracked / cracking it later: deferred, USER 2026-10-27 (docs/design/uncracked-fetchland.md).
