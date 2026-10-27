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
  across thread counts / interleavings (the batch's [determinism] WARNING). Once in the suite its GT would not be
  reproducible at a different thread count. The put-back single pick removes it (123 MB per worker). Default: disclosed;
  no pool change made.
- Q35. LTP measured neutral (+0.005, se 0.037) and 8% cheaper; its 11 losses are shuffle luck by the salt ensemble (still
  slower in 1 of 11 under four other search shuffles). The Tarn rule is the only USER rule with a clear gain (-0.06 turns);
  5 of its 18 losses persist across salts (gi 25, 61, 130, 154, 184 of seed 81000 d3) -- root-cause them if the rule is to
  be refined. Myriad neutral. Ulamog / Song amendments inert or neutral in this sample. Default: all as built (USER rules
  ON, amendments OFF).
- Q36. Q26 (a) (fold the decline / take-and-Remand / j variants of a seed-only plan whose Cyclone walk is EXACTLY
  predictable) is not built: the whole seed-pin axis is 2.7% of the work. Owed as a sound collapse. Default: deferred.
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
## Approved deferrals
- Instant timing collapsed to main phase (Brainsurge, Long-Term Plans, Curator / Wraith cycling): USER-approved 2026-10-27 ("All spells are cast at sorcery speed here").
- Street Wraith swampwalk (and its Landwalk tag): inert vs the never-blocking goldfish opponent. USER-approved 2026-10-27 ("fine to treat as inert until 1v1 play").
- Holding a fetchland uncracked / cracking it later: deferred, USER 2026-10-27 (docs/design/uncracked-fetchland.md).
