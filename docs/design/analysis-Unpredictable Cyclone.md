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
| Long-Term Plans | 2 | new `tutor_library_position 3` (NOT tutor_to_top: routes to anti and ~30 sites read it as next-draw); provider TutorSearchWidth 32 |
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

**TODO (phase 3):** when the Cyclone/Emrakul/Curator entries land, the deck routes to `Cyclone`: update the pin
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
- Q7. Instant timing collapsed to main phase for Brainsurge / Long-Term Plans / Curator & Wraith cycling
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
- Q14. ANSWERED 2026-10-27: USER *"Scalding Tarn sacrificing and finding nothing should indeed allow for a shuffle."* -> FIX QUEUED LAST, after phases 2-4 (USER: "can happen after everything else ... It isn't a key change"; generic engine fix in PerformFetch; may move other decks whose fetch can whiff -- measure in smoke/regression as a play change). Holding / late-cracking a fetch stays DEFERRED (USER: "isn't often super relevant"), recorded in docs/design/uncracked-fetchland.md. Original question:
  "then shuffle" (PerformFetch returns early). Rules-wrong but rare; fixing it moves any deck whose fetch
  whiffs. Default: disclosed, not changed (phase 1).
- Q15. APPROVED 2026-10-27: USER *"Swampwalk is fine to treat as inert until 1v1 play."* Original: Street Wraith swampwalk (+ Landwalk tag) inert vs the never-blocking goldfish -- PROVISIONAL deferral.
- Q16. Annihilator: the passive opponent sacrifices its OLDEST permanents first (a fixed rule for the
  opponent's choice; every spawn is an equally good Ulamog target). Default: oldest first.
- Q17. Two targeted-cast-trigger casts in one turn (Ulamog CH + Gyre) share ONE searched rank policy (the
  landfall_target_choice whole-turn shape), so their target choices are not searched independently.
  Default: accepted (both in one turn needs 21 mana or two Cyclone hits); flagged for phase 3.
- Q18. The turn_number classification used an exhaustive grep of all 954 reads instead of a compile-break
  rename (the identifier has no macro/token-pasting use, so the two list the same sites). Default: grep
  accepted; a rename pass can be run on request.

## Approved deferrals
- Street Wraith swampwalk (and its Landwalk tag): inert vs the never-blocking goldfish opponent. USER-approved 2026-10-27 ("fine to treat as inert until 1v1 play").
- Holding a fetchland uncracked / cracking it later: deferred, USER 2026-10-27 (docs/design/uncracked-fetchland.md).
