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
  who decides. The
  hand-cast self-Remand (riding a cast from hand) is PRUNED in CycloneProvider as a provider restriction on
  the USER's word (agent reading of "only off cyclone triggers"; surfaced to the USER 2026-10-27).
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
- Q9. Scalding Tarn fail-to-find: a crack with no Island/Mountain card left pays the life but skips the
  "then shuffle" (PerformFetch returns early). Rules-wrong but rare; fixing it moves any deck whose fetch
  whiffs. Default: disclosed, not changed (phase 1).
- Q10. Street Wraith swampwalk (+ Landwalk tag) inert vs the never-blocking goldfish -- PROVISIONAL deferral.

## Approved deferrals
(none yet)
