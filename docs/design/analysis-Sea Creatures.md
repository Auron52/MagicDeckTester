# Analysis ledger — Sea Creatures

Per-deck ledger for `decks/Sea Creatures/Sea Creatures.cod`, per the `analyze-deck` skill. Updated
continuously. Resume point: `docs/design/sea-creatures-analysis-checkpoint.md`.

**Run started:** 2026-11-16 (Claude orchestrator session). Worktree `.claude/worktrees/sea-creatures`,
branch `sea-creatures-analysis`, base commit `8cd0bbf23` (rebased 2026-11-16 from f92bb3b7c) (shared branch `phase-1-2-deck-analyzer`).
Subagents never push the shared branch. Scryfall JSON for the new cards:
`/workspaces/MagicDeckTester/build/scratch/sea/scry/`.

## Decklist (60 main, no sideboard; no wish)

| n | card | role | status at Stage 1 |
|---|---|---|---|
| 4 | Utopia Sprawl | Enchant Forest land aura (+1 chosen colour) | implemented |
| 4 | Wild Growth | land aura (+G) | implemented |
| 4 | Overgrowth | land aura (+GG) | implemented |
| 1 | Sol Ring | rock | implemented |
| 3 | Worn Powerstone | rock, enters tapped, {C}{C} | **MISSING** |
| 2 | Whelming Wave | sorcery: bounce all non-sea creatures | **MISSING** |
| 4 | Summon: Leviathan | 6-mana Saga creature 6/6 ward 2: I bounce non-sea creatures; II-III attack-draw | **MISSING** |
| 3 | Serpent of Yawning Depths | 6-mana 6/6, sea-creature evasion | **MISSING** |
| 4 | Spawning Kraken | 6-mana 6/6, sea creature combat damage -> 9/9 Kraken token | **MISSING** |
| 2 | Koma, World-Eater | 7-mana 8/12 trample ward 4, combat damage -> four 3/3 Serpents | **MISSING** |
| 2 | Hullbreaker Horror | 7-mana flash 7/8, cast trigger: bounce up to one | **MISSING** |
| 2 | Kiora Bests the Sea God | 7-mana Saga: 8/8 hexproof Kraken; tap opp; steal | **MISSING** |
| 1 | Sin, Unending Cataclysm | 7-mana 5/5 flying trample, as-enters counter removal | **MISSING** |
| 1 | Trench Gorger | 8-mana trample, ETB exile any number of library lands = base P/T | **MISSING** |
| 1 | Lorthos, the Tidemaker | 8-mana 8/8, attack: may pay {8} tap up to eight | **MISSING** |
| 2 | Brinelin, the Moon Kraken | 8-mana 6/8, ETB + MV>=6 cast: bounce up to one | **MISSING** |
| 4 | Breeding Pool | shock | implemented |
| 4 | Misty Rainforest | fetchland (OLD in-hand model) | implemented — **GAP** |
| 2 | Windswept Heath | battlefield fetchland | implemented |
| 1 | Flooded Strand | battlefield fetchland | implemented |
| 1 | Flooded Grove | filter land | **MISSING** |
| 1 | Hedge Maze | Forest Island, tapped, surveil 1 | **MISSING** |
| 5 | Forest, 2 Island | basics | implemented |

## Stage 1 — Coverage (DONE 2026-11-16)

* missing (14): Trench Gorger, Summon: Leviathan, Sin, Worn Powerstone, Lorthos, Serpent of Yawning
  Depths, Koma, Spawning Kraken, Hullbreaker Horror, Brinelin, Kiora Bests the Sea God, Whelming Wave,
  Flooded Grove, Hedge Maze.
* `fetch_hold_review`: Misty Rainforest `hold_modelled: false` (old immediate-on-play model);
  Windswept Heath / Flooded Strand already the battlefield model. **Agent default (SURFACED, PROVISIONAL):**
  migrate Misty Rainforest to the battlefield fetchland model, as Boars did for Windswept Heath.
  **USER 2026-11-16: migrated for the VIEWER's hold only; searched play never holds (Open USER
  questions #0). Breeding Pool fetched first (#1).**
* Every creature in the list is a Kraken / Leviathan / Octopus / Serpent, so the "except sea creatures"
  bounces (Whelming Wave, Summon: Leviathan I) only ever hit the opponent's spawned creatures, which
  never block or attack: faithful but payoff ~0 in goldfish.

## Stage 2 — research drafts

| draft | cards | file | status |
|---|---|---|---|
| combat | Koma, Spawning Kraken, Serpent, Lorthos, Whelming Wave | logs/sea-research/combat.md | DONE: Spawning Kraken Tier 1 (Millicent per-creature `FireCombatDamageTokens`, shared executor/rollout); Koma Tier 2 (`combat_damage_watch_self` + optional token-name param "Koma's Coil"); Serpent Tier 1 + inert-evasion note; Lorthos Tier 1 + dominance fold (always decline {8}); Whelming Wave Tier 2 (bounce helper shared with Summon I, + Merfolk). No second-main need. Proposed provider fold: skip casting Wave unless Hullbreaker on battlefield/in hand (default ON, `MTG_UNPRUNE=seawave`) -> USER Q. PROVISIONAL deferrals: Serpent evasion (Master of the Pearl Trident precedent, USER-approved 2026-11-01), Lorthos tap/freeze. Cross-notes: Kiora token subtype Kraken; stolen creature keeps opponent owner. |
| sin | Sin, Unending Cataclysm | logs/sea-research/sin.md | DONE: Tier 3 medium-small, no new struct fields (`Permanent::lore_counters` exists). Shared removal helper at `FireEtbWatchers` (every entry route), 2x as +1/+1. Choice = live-probe subset pin (`@H?` -> `@H<n>,<n>`, the Life-from-the-Loam probe), fold identical copies to k-of-m, no lossy prune; rollout default harvest every Saga. Viewer bucket A (`dragon` multi-pick). Risk: pin read only by Sin's own entry; probe kept out of the TT. Constraint for sagas: never assume lore>=1; a reset Saga re-fires chapter I. PROVISIONAL deferral: dies trigger (Sin cannot die in this list). |
| gorger | Trench Gorger | logs/sea-research/gorger.md | DONE: Tier 3, no deferral. Param `etb_exile_library_lands_set_base_pt`, shared resolver beside Primeval Titan's ETB. Decline = 6/6 no shuffle; search always shuffles; exile 0 -> 0/0 dies (pruned for AI, offered to human). Branch on the in-plan land-search pin: provider-ordered keep menu (~54 entries, <62 cap): exile-all (default + rollout), keep one of each kind, every keep-two, then searched count N with provider-chosen lands for N>=3 (DISCLOSED LOSSY, Genesis Wave precedent -> USER Q), + decline. Cheaper ~10-entry menu drafted as default-OFF lever. Per-card reshuffle keeps other cards' order (thinning valued on the seed's real draws). Viewer bucket A (dig yes/no + dragon multi-pick). Risks: bounced Gorger keeps N/N in hand (fix for Gorger; Figure of Destiny latent same bug -> USER Q, moves GT); sim/dominance keys need power folds. |
| sagas | Kiora Bests the Sea God, Summon: Leviathan | logs/sea-research/sagas.md | DONE: Tier 2 on the EXISTING Saga engine (World War Hulk, docs/design/saga-world-war-hulk.md) + 3 Tier-3 pieces: turn-scoped sea-attack draw in shared `ApplyAttackDrawTriggers`; Kiora III searched steal via the chapter-target pin + Bolas/Expropriate control change; creature-Saga sacrifice via `SacrificePermanentAt`. Summon attacks exactly once (sacrificed before combat on III's turn). `DeckUsesSecondMain` flips on the new attack-draw param. Sin needs only lore=0. RISK: Summon I is the first permanent-REMOVING chapter -> audit held battlefield indices in both worlds. Chapter-target axis gated to the NEXT chapter (World War Hulk byte-identity via Stompy). PROVISIONAL deferral: Kiora II don't-untap rider (Abominable Treefolk precedent). Card name with ':' (path safety). |
| mana | Worn Powerstone, Flooded Grove, Hedge Maze, Misty Rainforest, fetch + Sprawl prunes | logs/sea-research/mana.md | DONE: Powerstone Tier 1 (Fire Diamond + Sol Ring shape); Grove Tier 1 (Fire-Lit Thicket, produces [G,U]) + F2 payer fix (land-aura bonus dropped when a filter land is tapped; Bruna untouched); Hedge Maze Tier 1 (Thundering Falls + Forest Island) + F1 rules fix (fetched Maze surveils BEFORE the shuffle: wrong, also legacy path); Misty Tier 2 battlefield migration (moves Creature Giving x1, Five Colour x2 play/GT; their keep tables/leaves stale -> USER; Mill byte-identical; refs predate the stamp). Hold: crack on play (root default). Fetch prune `MTG_SEA_FETCH_PRUNE` + `MTG_SEA_FETCH_ISLAND_FIRST` (Pool > Island > Forest > Maze; Boars low-life copy). Sprawl `MTG_SEA_SPRAWL_BLUE` (pips 44 U vs 17 G); exception E1 `MTG_SEA_SPRAWL_GREEN_NEED` default OFF. Prereq: SeaCreaturesProvider (deck likely routes to AntiLifegain today). PROVISIONAL D-1: fetched Maze surveil = provider heuristic. |
| bounce | Hullbreaker Horror, Brinelin | logs/sea-research/bounce.md | DONE: Tier 3. Shared `ReturnPermanentToOwnersHand` (also Whelming Wave / Summon I); cast triggers in `FireOnCastTriggers`; Brinelin ETB deferred past the enter cascade; per-instance searched target digit (Raggadragga machinery, ordinal renamed/shared), decline included; viewer `target` with min_targets 0. RISK: width (~20k combos late) -> measure at 5f, waves not caps. PROVISIONAL deferrals: Hullbreaker Flash (main-phase cast dominates), Brinelin Partner. Open: tap-in-response Sol Ring float; DeckUsesSecondMain for Hullbreaker post-combat re-buy. |

## Integration (serial)

| step | change set | commit | status |
|---|---|---|---|
| I1 | mana draft: Worn Powerstone; Flooded Grove + F2 (land-Aura bonus on EVERY greedy filter tap -- the draft's fallback branches 2/3/5 AND the ranked payer's kind-3 free-{C} / kind-2 fed taps, which the draft missed); Hedge Maze + F1 (searched-out look-land surveils AFTER the shuffle: PerformLandSearch, PerformFetch, and also PerformLandTutorToBattlefield (Crop Rotation) + Nissa -8, same defect); SeaCreaturesProvider (fetch prune MTG_SEA_FETCH_PRUNE / _ISLAND_FIRST on the legacy AND battlefield routes, Sprawl MTG_SEA_SPRAWL_BLUE, E1 MTG_SEA_SPRAWL_GREEN_NEED off); Misty Rainforest battlefield model for the HUMAN only (fetch_hold_human_only: autonomous keeps the immediate route, no searched / <=3-life hold) | 4eb5a3e10 (core), 4cf605b4f (Misty, last commit, droppable) | DONE. Units 3377/3377 green. Byte-identity d3 b10 40 g: 4eb5a3e10 vs base = digests+units identical on Mill, Bruna, Boars, Spirits, Treasure Hunt, Creature Giving, Five Colour, Jumbo Cactuar, Accomplished Alchemist, Hinata. 4cf605b4f vs 4eb5a3e10: all identical incl. Five Colour + Sea (also d3 b0) EXCEPT Creature Giving (units, 1/40 play digest gi21, b10 and b0) -- root-caused to CardParams LAYOUT, not Misty: commit 4eb5a3e10's src plus ONE unused bool in CardParams reproduces the exact moved digest (16009bd3, 338192 u); Misty's new code paths are inert there (the old cards.json on the new binary moves identically). Also observed: Creature Giving's d3 b10 result depends on the pooled batch's job mix (same binary). Both pre-existing (latent CG layout/history sensitivity) -> orchestrator/USER. **WITHDRAWN (I3, 2026-11-17):** not engine nondeterminism -- an A/B artifact: the base binary had been run against the NEW cards.json. With each arm pinned to its OWN commit's cards.json, Creature Giving is byte-identical (I3 row). See docs/design/creature-giving-jobmix-nondeterminism.md (commit 6db0c4477, branch cg-jobmix-fix; the coordinator brings it onto this branch). Sea smoke d3 b10 40 g: no crash, 0/40 won (avg 9.0; every creature still missing). Owed: interim provider signature (staple shapes + Kraken watch) -> later steps OR in Sea-only params and drop the staple terms; cost audit rc 0; field snapshot added for the 3 new cards from the local Scryfall JSON (--update was 429-limited); Misty's fetch_hold_review now hold_modelled true. |
| I2 | combat draft: Spawning Kraken (data); Koma (combat_damage_watch_self + combat_damage_token_name "Koma's Coil" through BuildTokenProto/CreateTokens); Serpent of Yawning Depths (Enchantment Creature, evasion PROVISIONAL Q5); Lorthos (attack_may_pay_tap_permanents recorded, always declined, PROVISIONAL Q5); Whelming Wave (PerformBounceAllCreaturesExcept, executor + rollout); SHARED ReturnPermanentToOwnersHand (BounceOpponentNonlandAt now wraps it; token / opponent spawn ceases; no inline CR 704.5m sweep -- callers at a safe point sweep, the mass bounce does); provider NeverCast for Wave (MTG_SEA_WAVE_NEVER, MTG_UNPRUNE=seawave, USER Q6); signature += Koma / Wave terms | fac368913 | DONE. Units 3386/3386. Byte-identity d3 b10 40 g (Fungus 24) vs 8682648b6: wins + digests + units identical on Merfolk (Sink into Stupor -> the wrapper), Spirits, Fungus, Giants, Boars. Sea smoke d3 b10 40 g: no crash, 37/40 won, avg 6.20 (still missing Sin, Trench Gorger, Kiora, Summon: Leviathan, Hullbreaker, Brinelin). Cost audit rc 0; field audit rc 0 (snapshot from local Scryfall JSON); viewer static audit clean; provider_audit --check rc 0. Owed: staple signature terms kept (3 Sea-only cards, one the never-cast Wave) -- drop after I3/I4; I3/I4 must call ReturnPermanentToOwnersHand and sweep orphaned Auras at their own safe point. |
| I3 | sagas draft: Kiora Bests the Sea God (custom Saga: I 8/8 blue hexproof Kraken token; II tap all opposing nonland permanents; III gain control of target opposing permanent + untap, SEARCHED via the WWH chapter-target rank pin over a provider-restricted list) and Summon: Leviathan (Enchantment Creature Saga 6/6 ward 2: I PerformBounceAllCreaturesExcept to OWNERS' hands; II/III per-instance GameState::eot_attack_draw_n -> draw per declared sea attacker in ApplyAttackDrawTriggers, both worlds); FireSagaChapter runs every effect keyed to a chapter; AdvanceSagas CR 714.4 `>=` and lore-0 re-fires I (Sin); creature Saga sacrificed via SacrificePermanentAt; shared GainControlOfPermanent; root RestrictSagaStealCandidates (identity + dominance folds on vanilla bodies, MTG_UNPRUNE=sagasteal); SeaCreaturesProvider conditional restricted main 2 while eot_attack_draw_n > 0 (full menu); Sea signature now Sea-only params (I1 staple terms DELETED) | 407838f7b | DONE. Units 3399/3399 green (13 new in test/unit/test_sea_sagas.cpp, incl. executor AND rollout chapter-I cast drain). Byte-identity d3 b10 40 g seed 7730001, ONE pooled batch per arm, EACH ARM ON ITS OWN COMMIT'S cards.json (base 403b7f58c): digests + units + all 280 [win] lines identical on Stompy Surprise (WWH), Five Colour (Two-Headed Hellkite attack draw), Merfolk (Svyelun attack draw + bounce wrapper), Vampires, Spirits, Creature Giving, Hinata (control). Provider ROUTING: `--cast-order-report` `# provider:` identical base vs new on all 57 shipping decks + 11 archived lists. Sea smoke d3 b10 40 g: no crash, 40/40 won, avg 5.925 (I2: 6.20); MTG_SAGA_TRACE 12 g: chapter I/II/III all fire, executor log shows the Kraken token on Kiora's cast turn. Still missing: Sin, Trench Gorger, Hullbreaker Horror, Brinelin. Cost audit rc 0 (664/664); field audit rc 0 (snapshot rows for the 2 cards from the local Scryfall JSON); provider_audit --check rc 0; viewer static audit rc 0 (saga params classified: chapter targets -> `target`, WWH I -> `free_cast`, the rest INERT; INERT entries need the USER's OK, Q16(f)); DECISIONS.md reuse row for Kiora III. Owed: eot_attack_draw_n not yet in the RouteSig pin vector / stored-now diff / viewer JSON (optional); value-leaf features read neither lore nor eot_attack_draw_n; Q12 every-turn arm (MTG_FORCE_USES_M2=1) to measure before Stage 5j; Stompy Surprise's viewer audit still fails on Ghalta's etb_put_creature_cards_from_hand (pre-existing, not Sea). |
| I4 | bounce draft: Hullbreaker Horror + Brinelin, the Moon Kraken. ONE optional bounce body (FireBounceTriggerInstance -> ReturnPermanentToOwnersHand + one SweepOrphanedAuras at the trigger's resolution) fired from FireOnCastTriggers (Hullbreaker every spell, mode 1 'spell you don't control' over the stack, always empty; Brinelin printed MV >= 6; neither off its own cast) and from Brinelin's ETB (g_pending_etb_bounces, drained AFTER the legend rule in both worlds + both turn-start nets). Search: either side legal, opponent side folded away (sound), identity fold, DECLINE first, provider order hook (order only), one digit per instance on the Raggadragga I4c machinery (ordinal renamed cast_target_trigger_ordinal, shared; BounceTriggerAxisWidth -> AppendRaggaInstanceVariants; site-10 breakpoint bit). USER Q6: SeaCreaturesProvider::BounceTriggerDeclineOnly declines every instance (no digit) -- MTG_SEA_HULLBREAKER_DECLINE / MTG_SEA_BRINELIN_DECLINE default ON, =0 searches the FULL per-instance digit (not the first-instance fallback); MTG_UNPRUNE=seabounce. Viewer: g_play_target_chooser, min 0, '(no target)' declines. Flash / Partner PROVISIONAL (Q4). Legend rule = the existing searched legend_keep (verified: generic for every legendary cast with a twin out) | 8e1be70c1 | DONE. Units 3409/3409 (10 new, test/unit/test_sea_bounce.cpp: executor AND rollout ETB-after-legend-rule, per-instance digits, Raggadragga + bounce sharing the ordinal, token ceases, Aura returns, MV gate, provider decline, human chooser both ways). Byte-identity d3 b10 40 g seed 7740001, one pooled batch per arm, each arm on its OWN commit's cards.json (base 7374f09b1): digests + units + all 240 [win] lines identical on Boars (ordinal rename), Unpredictable Cyclone (Ulamog cast-target axis), Auras (Kor Spiritdancer), Kitty Equipment (Sram), Merfolk (bounce wrapper), Hinata (control). Sea smoke d3 b10 40 g: no crash, 40/40 won, avg 6.225 (default); =0 arm (both prunes off) digest-identical, avg 6.225, units +15.5%; 16 logged =0 games executed 0 bounces (24 turn-phases with a source out). Still missing: Sin, Trench Gorger. Cost audit rc 0 (666/666); field audit rc 0 (snapshot rows from the local Scryfall JSON); provider_audit --check rc 0; viewer static audit rc 0 (bounce params -> `target`, STATE_GATED entry pinned by the unit test); DECISIONS.md reuse row. Owed: see Q17. |
| I5 | sin draft: Sin, Unending Cataclysm. CardParams::as_enters_harvest_counters (2); ONE body ApplyCounterHarvestAsEnters in the universal enter cascade (FireEtbWatchers, after daybound), both worlds, every entry route; not targeted; RemoveAllCounters shares ONE field list with TotalCountersOn (ForEachDedicatedCounterField); 0-toughness via the doomed queue; PutPlusCounters (doubler-aware). Search: "@H?" stamped in CollectActions (ETB-choice decision-space gate), AppendSubdecisionAxes live-probe fan after the Loam fan (sees earlier same-plan casts and cast-trigger bounces), "@H<n>,<n>" per subset, identity fold k-of-m, no truncation; pin via CounterHarvestPinScope keyed on the entrant's per-copy number (executor EnterBattlefield + rollout apply_one); probe folded out of the mid-turn key (0x4A2B5E57); "@H?" bypasses the ordering dedup; #HV in both name-dedup keys; EvalCard credits the default harvest. Base CounterHarvestDefault: opposing, own Sagas, own -1/-1-only, own +1/+1-only creature that cannot attack. **USER 2026-11-17 (verbatim): "We should search in the engine for sin, but heuristically prune so that it strips sagas. I don't think there are any other counters in the deck, but if anything else shows up I guess we can leave it?"** -> SeaCreaturesProvider::CounterHarvestSearchSubsets = ONE subset (every Saga with lore, nothing else), MTG_SEA_SIN_STRIP_SAGAS default ON (=0 the full engine menu), MTG_UNPRUNE=seasin; Sea CounterHarvestDefault = the same rule. Viewer: `dragon` multi-pick, every candidate, default preselected, empty reply stands. Dies trigger PROVISIONAL (Q9) | 03c800faf | DONE. Units 3420/3420 (11 new, test/unit/test_sea_sin.cpp: field-list twin, executor pins incl. bounced-first skip, base vs Sea default, sentinel = default, pin not consumed by another entrant, harvested Saga re-fires chapter I, Sea prune = one pin and =0 = 8, k-of-m fold, rollout apply lockstep, human chooser). Byte-identity d3 b10 40 g seed 7750001, one pooled batch per arm, each arm on its OWN commit's cards.json (base 314dbfc9c): digests + units + all 240 [win] lines identical on Stompy Surprise (WWH lore), Gods (Life from the Loam probe), Bruna (dragon multi-pick), Landfall (Genesis Wave dragon), Hydras (+1/+1 counters, TotalCountersOn), Hinata (control). Sea smoke d3 b10 40 g seed 7740001: no crash, 37/40 won, avg 6.15 (loss = 9); the =0 arm is digest-identical. CORRECTION to I4: I4 was also 37/40 (the same gi 6/21/33 lost; avg 6.225 counted them as 9), not 40/40. Still missing: Trench Gorger only. Composition note for I3->I4 (5.925 -> 6.225): a missing card stays in the deck as a definition-less placeholder (DeckLoader::MakePlaceholder; every reader skips a null def), i.e. a DEAD SLOT, so the move is NOT composition; it is a play change (Hullbreaker / Brinelin went from dead to castable), not investigated further (~1.5-2 se at 40 g). Cost audit rc 0 (667/667); field audit rc 0 (snapshot row from the local Scryfall JSON); provider_audit --check rc 0; viewer static audit rc 0. Owed: see Q18. |
| I6 | gorger draft: Trench Gorger. CardParams::etb_exile_library_lands_set_base_pt (the loader refuses it with land_search_count). ONE body, PerformExileLibraryLands (LandSearch.h), in the universal own-ETB cascade (FireOwnEtbTriggersBody, beside Primeval Titan), both worlds, every entry route. DECLINE = no search, no shuffle, stays 6/6. SEARCH = exile, ONE shuffle (also at 0), then base P/T = N if it is still on the battlefield (layer 7b, the Figure shape). 0 exiled = 0/0, dies through the doomed queue. Search: one in-plan land-search ordinal per cast (and per Genesis Wave put of a Gorger) over a menu of land classes: exile all (front), keep one per class, EVERY keep-two, keep-j (j>=3) as ONE provider-ordered round-robin prefix per count (DISCLOSED LOSSY, Q19a), cap 62 (truncated 0 here), plus the searched decline (pin 63). Root provider: ExileLibraryLandsKeepOrder (ORDER only) and the 'exile 0' prune (MTG_UNPRUNE=gorgerzero). **USER 2026-11-16 (verbatim): "Trench Gorger is likely a "take all lands" type of effect since it is at the top of the curve, but we'll have to do some testing." ... "Actually I guess less than 6."** -> SeaCreaturesProvider: >= 6 library lands -> {exile all}; < 6 -> decline. MTG_SEA_GORGER_ALL default ON (=0 the searched menu), MTG_UNPRUNE=seagorger, off for human play. Keys: BuildSimKeyBase, DomPerm, BoardSignature and PermIsPlainForFold fold the base P/T (param-gated). No memo or pick-order cache reads the menu, so no cache-key bit. Q11 bounce reset: RevealedCard drops the override for Gorger's param only. Viewer: `dig` (search?) then `dragon` over EVERY library land, default all, empty reply stands. DECISIONS.md reuse row. Trace MTG_GORGER_TRACE=1 | ddf109ddc (code), 9515c52d3 (snapshot + validation) | DONE. Units 3429/3429 (9 new, test/unit/test_sea_gorger.cpp). Byte-identity d3 b10 40 g seed 7760001, one pooled batch per arm, each arm on its OWN commit's cards.json (base e9815ca9c): digests + units + all 240 [win] lines identical on Landfall (Primeval Titan, Genesis Wave, Cultivate, Evolving Wilds), Selesnya Lifegain (Genesis Wave), Kithkin (Figure of Destiny), Boars (battlefield fetch), Merfolk (bounce wrapper / RevealedCard), Hinata (control). Sea smoke d3 b10 40 g seed 7740001: no crash, 37/40, avg 6.125 (loss = 9; I5 6.15). The only mover is gi35 (7 -> 6). It still loses gi 6/21/33: gi6 mulligans to 5 and is stuck on 4 mana sources by T7, never casting a spell; gi21 mulligans to 6, sits on 4 sources T2-T5 and casts its first creature T7 (opponent at 14 at T8); gi33 casts Gorger T7 and exiles 16 lands for a 16/16, which swings T8 to put the opponent at 4, 4 short at the turn-8 horizon. Only 4 Gorgers resolved (12-16 library lands, all exile_all). The MTG_SEA_GORGER_ALL=0 arm is digest-identical (+7.8% units). Its entry census: exile_all 4, decline 0, keepK 0. Cost audit rc 0 (668/668); field audit rc 0 (snapshot row from the local Scryfall JSON, --update 429-limited); provider_audit --check rc 0; viewer static audit rc 0 (dig + dragon; all 24 Sea defs present, NO card missing). Owed: see Q19. |

## Open USER questions (surfaced, not blocking)

0. **Misty Rainforest hold (USER 2026-11-16, three messages in order):** *"We don't need the hold option
   for those decks or for this deck."* / *"(in searched)"* / *"The viewer can keep it."* So: Misty IS
   migrated to the battlefield model so a HUMAN can hold it in the viewer; SEARCHED play never holds it
   (crack on play, no new <=3-life autonomous hold) in Creature Giving, Five Colour or Sea Creatures;
   their autonomous play is to stay byte-identical where sound, and any remaining mover is reported to the
   USER with its mechanism. Heath/Strand/Foothills behaviour unchanged.
   **General rule (USER 2026-11-16):** *"Hold should be per-deck and focused on cases with landfall."*
   *"Because the only reason to hold otherwise is clairvoyance."* (cf. the 2026-10-13 ruling: clairvoyant
   wins are not regressions.) Sea Creatures has no landfall, so it never holds in search. OWED FOLLOW-UP (not in this run's scope;
   would move other decks): audit every existing SEARCHED hold site against this rule -- the root
   <=3-life autonomous hold (`FetchResolvesOnPlay`) is not landfall-scoped -- and bring the list to the USER.
   USER on life: *"Holding is not the correct solution to that problem"* -- fetch a shock tapped or a basic
   instead. Deferred doc: `docs/design/fetch-hold-searched-scope.md`.
1. ~~Misty Rainforest: migrate to the battlefield fetchland model~~ — **ANSWERED (USER 2026-11-16):**
   *"Yes, misty rainforest should automatically get Breeding Pool, kind of like how Boars gets Stomping
   Ground."* So: migrate Misty to the battlefield model, and the Sea Creatures provider ranks Breeding
   Pool first on the fetch target list while it remains in the library (the Boars shock-first shape,
   commit 910e1959f, `MTG_BOARS_SHOCK_FIRST`), default ON with an =0 off switch.
   **Refined (USER 2026-11-16):** *"These should be heuristically pruned. Though we might want to
   prioritize Island over forest once the breeding pools are gone because of the composition of the
   deck."* So the rule is a PROVIDER ONE-OPTION PRUNE (not a reorder): Breeding Pool while one remains,
   else Island over Forest. Applies to Misty / Windswept Heath / Flooded Strand. Human play still offers
   every target; =0 restores the full searched set. Island-vs-Forest stays switchable for a Stage 5 A/B
   (the USER said "might").
2. Hedge Maze (Forest Island, enters tapped, surveil 1) as a fetch target: where in the pruned order?
   Agent default (PROVISIONAL): after Island and Forest.
4. Hullbreaker Flash / Brinelin Partner deferrals (inert: main-phase cast dominates; Commander-only).
   PROVISIONAL.
5. Serpent of Yawning Depths evasion + Lorthos {8} tap (always declined, dominance) deferrals. PROVISIONAL.
6. ~~Whelming Wave cast-skip~~ — **ANSWERED (USER 2026-11-16):** *"Yes, I would heuristically skip
   Whelming Wave. Neither of Hullbreaker's abilities are useful in Goldfish, so we can opt out
   heuristically as well."* So: (a) the Sea Creatures provider never casts Whelming Wave (unconditional
   prune, no Hullbreaker exception); (b) the provider always DECLINES Hullbreaker Horror's cast trigger
   (both modes). Both default ON, each with an =0 flag restoring the searched option. The engine still
   implements both cards faithfully and the viewer still surfaces them to a human.
   **Brinelin (USER 2026-11-16):** *"Brinelin is the same deal. Unless you can come up with a
   counterexample I would prune it."* Agent's candidate counterexamples, both judged WEAK and surfaced:
   (i) re-buy a tapped Sol Ring (recast {1}, tap for {C}{C}: net +1 mana that turn); (ii) bounce a Kiora
   Bests the Sea God / Summon: Leviathan that already resolved chapter I and recast it (another 8/8, or
   another Summon cycle) -- costs 6-7 more mana, i.e. no better than casting another threat from hand.
   Both apply to Hullbreaker too. So: Brinelin's ETB + cast trigger ALSO always declined by the provider
   (default ON, own =0 flag). Stage 5 A/Bs the bounce prunes (=0 vs default) so either counterexample
   shows up if real; a measured case gets a narrow exception, not a dropped prune.
7. Hullbreaker post-combat bounce-and-recast: covered by the deck's second main (Summon draws turn it on).
8. Tap-Sol-Ring-in-response-then-bounce: not expressible today; follow-up, not built now.
10. Trench Gorger exile menu — **USER 2026-11-16:** *"Trench Gorger is likely a "take all lands" type of
    effect since it is at the top of the curve, but we'll have to do some testing."* Plan: the engine
    implements the full menu (~54 entries incl. decline); the provider adds a one-option prune to
    EXILE ALL LANDS (default ON, `MTG_SEA_GORGER_ALL`-style flag, =0 restores the searched menu; viewer
    keeps the full choice). Stage 5 A/Bs exile-all vs the searched menu and reports which menu entries
    the searched arm actually picks, before anything is settled. USER rationale: *"The idea is to increase
    its size and also increase the chance of drawing gas."* (Both are modelled: base P/T = lands exiled,
    and the per-card reshuffle means the search sees the thinned library's real draws.)
    **Threshold (USER 2026-11-16):** *"If we had only a few lands left (less than 7) it wouldn't be worth
    it, but I'm not sure how likely that is to occur."* then *"Actually I guess less than 6."* So the
    provider prune is: library holds >= 6 lands -> exile all; < 6 -> decline (stay 6/6). Agent estimate
    (60 cards, 20 lands incl. 7 fetches, Monte Carlo): library lands at T6/T7/T8/T10 average
    14.6/14.1/13.7/12.8, and P(<7) is 0.02%/0.07%/0.15%/0.56% -- the decline branch almost never fires.
11. Figure of Destiny latent bug (bounced card keeps its base-P/T override in hand). Fixing it moves
    GT for decks with Figure. Agent default: fix for Gorger now; Figure as a separate USER-approved change.
12. Main 2 shape (USER-owned): every turn (costly vs the 3x-Hinata gate) or the cheaper Omnitool-style
    main 2 opened only on turns with combat draws (Summon II/III). Agent default (PROVISIONAL): the
    conditional main 2; measure both before Stage 5j.
13. Kiora II "don't untap during next untap step" deferral (the opponent never untaps; setting
    skip_next_untap would wrongly keep a later-stolen creature tapped). PROVISIONAL.
9. Sin's dies trigger (counters to target creature, shuffle into library) deferral: Sin cannot die in
   this list. PROVISIONAL.
3. ~~Utopia Sprawl colour~~ — **USER 2026-11-16:** *"Utopia sprawl should prioritize blue as well. Green
   is a minor colour in the deck."* Implemented as: the provider's ETB colour axis restricted to {U, G}
   (W/B/R have no consumer: sound fold), plus a default-ON one-option prune to BLUE (=0 restores {U, G}
   as searched branches). Viewer keeps the full choose_color list. **Confirmed (USER 2026-11-16):** *"In
   fact, we should try a prune to always use blue."* No state exceptions; Stage 5 A/Bs always-blue vs
   searched {U, G} and reports the result to the USER. **USER 2026-11-16:** *"(and to be fair, if we need
   to build an exception for a specific case we probably can just do that rather than dropping the
   prune)"* — a measured problem case gets a narrow exception; the prune stays.

14. **I1 (agent defaults taken, surfaced):** (a) PROVISIONAL deferral D-1: a FETCHED / searched-out look-land's surveil is the provider's top-disposition rule, not a searched axis (after F1 it is the post-shuffle card; under the prune a fetched Maze is practically unreachable); human play asks. (b) F1 was applied to Crop Rotation's PerformLandTutorToBattlefield and Nissa -8 as well (same rules defect; inert for every current list). (c) The Sea fetch prune keeps the Boars low-life copy (<= 3 life: full held list) and Heath / Strand keep the root <= 3-life autonomous hold -- both unreachable in goldfish Sea (floor 5 life) and both inside the deferred `docs/design/fetch-hold-searched-scope.md` audit. (d) Misty Rainforest: `fetch_hold_human_only` is a new name-free param (the Tarn/Delta shim generalised per card); its `produces` W/U/B/R/G hand proxy is kept (Tarn shape) so no hand reader moves. (e) ~~Creature Giving moves on ANY CardParams layout change~~ -- **WITHDRAWN (I3, 2026-11-17):** it was an A/B artifact (the base binary run on the NEW cards.json), not engine nondeterminism; with each arm on its own cards.json, I3 (which adds CardParams fields AND a GameState field) is byte-identical on Creature Giving. See docs/design/creature-giving-jobmix-nondeterminism.md (commit 6db0c4477, branch cg-jobmix-fix; the coordinator brings it onto this branch). Every byte-identity A/B must pin each arm's OWN commit's cards.json.
15. **I2 (agent defaults taken, surfaced):** (a) Whelming Wave and the cleanup discard: the provider never casts it,
    so it is a dead card in hand -- whether the discard ladder should shed it first is USER-owned (discard buckets);
    nothing changed. (b) Cast order: none needed for Wave while it is never cast. (c) A scheduled opponent spawn
    bounced by ReturnPermanentToOwnersHand now CEASES (the engine models spawns as tokens) instead of entering the
    opponent's hand -- this also applies to Sink into Stupor (Merfolk); byte-identical on Merfolk d3 b10 40 g.
16. **I3 (agent defaults taken, surfaced):** (a) Q12 main-2 shape default = the CONDITIONAL provider-restricted main 2, opened (full menu) on every turn a Leviathan II/III instance is live -- even when no sea creature attacked (rare); DeckUsesSecondMain NOT flipped; every-turn arm = MTG_FORCE_USES_M2=1. (b) Kiora II's don't-untap rider: PROVISIONAL deferral (Q13), skip_next_untap is not set. (c) Kiora III steal folds live in the ROOT provider (sound identity + dominance on vanilla bodies), default ON, MTG_UNPRUNE=sagasteal; not Sea-specific. (d) The three pre-existing open-coded control-change sites (Bolas -2, Expropriate, Captivating Vampire) are NOT migrated to GainControlOfPermanent; only Kiora uses it. (e) Disclosed latent gap: a NONcreature Saga's sacrifice reaches no sacrifice/LTB watcher (none exists for this list). (f) Viewer INERT classification of saga_chapters, saga_ch1_token_*, saga_ch2_tap_opp_nonland, saga_ch1_bounce_creatures_except_subtypes, saga_eot_attack_draw_{chapters,subtypes} needs the USER's OK. (g) 2HG: Kiora II taps EVERY opposing nonland permanent (the engine has one opposing controller), not one target head's. (h) Kiora III may steal an opposing LAND (it is 'target permanent'); the search picks among the restricted list, and the trace shows lands taken often when the opponent has no creature.
17. **I4 (agent defaults taken, surfaced):** (a) The Q6 bounce prunes are IN, default ON; an early, NOT-an-A/B signal for Stage 5: at d3 b10 on 40 games the =0 searched arm is outcome-identical to the prune (digest-identical, +15.5% units) and 16 logged =0 games executed no bounce -- so far no counterexample. (b) The OPPONENT-side fold is in the ENGINE as a sound weak dominance (the Raggadragga precedent; decline is always an option, and bouncing a passive opponent's permanent can only shrink our options); human play sees both sides. (c) The =0 order is the root's: DECLINE first, then battlefield order (rank 0 = decline is the beyond-horizon playout). The draft's base rule (Saga past chapter I and a tapped Sol Ring ranked ahead of decline) and its own-token provider restriction were NOT built -- both matter only at =0; propose them only if the Stage 5 A/B finds the searched arm winning. (d) Disclosed gaps (unreachable in this list): the nested free-cast callers of FireOnCastTriggers (cascade / suspend / ripple / Remand) are not audited for held battlefield indices; Brinelin's MV gate reads the PRINTED MV, so an {X} spell's X on the stack is not counted (no X spell here); BounceTriggerAxisWidth counts CastFromHand casts only; a deck with an Ulamog AND a bounce source would share the Ulamogs' flat rank (the existing Raggadragga OWED note). (e) The main.cpp viewer builder branch has no --claude-play repro yet (the unit test drives the chooser hook directly) -- the Stage 5d sweep should reach a Hullbreaker-out cast and confirm the `target` decision with '(no target)'. (f) DeckUsesSecondMain is NOT flipped for Hullbreaker (the draft's post-combat re-buy line is pruned by Q6; Q7 stands).

18. **I5 (agent defaults taken, surfaced):** (a) Q9 (Sin's dies trigger) stays PROVISIONAL. (b) CAST ORDER: Sin's live probe
    sees a Saga cast EARLIER in the same plan only when the deck's ordering puts it first; Life from the Loam forces
    ordering search for its plans, Sin does not (cast order is USER-owned; the 14-mana same-turn Saga + Sin line is rare).
    (c) Disclosed simplifications, unreachable here: a Genesis Wave co-entrant is visible to Sin (CR 614.12 says it should
    not be); a creature / artifact planeswalker harvested to 0 loyalty is not killed (CR 704.5i). (d) The new param is
    NOT in CardDatabase's walk-bound / hand-gain audited whitelists (conservative: Sin counts as unaudited; harvesting a
    storage or charge counter can change mana). (e) The prune and the =0 arm were digest-identical at d3 b10 40 g, so
    no counterexample so far (an early signal, not the Stage 5 A/B).

19. **I6 (agent defaults taken, surfaced):** (a) **DISCLOSED LOSSY SHAPE in the ENGINE menu** (the Genesis Wave
    precedent): for 3 or more KEPT lands only the COUNT is searched; WHICH lands are kept is one provider-ordered
    round-robin prefix per count (one of each class in keep order, then a second of each, ...). Keep-one-per-class and
    EVERY keep-two are searched exactly. The menu is also capped at 62 entries (block 3, then block 2, cut from the far
    end, counted in `truncated`; 0 for this list, ~54 entries). Both are budget-independent, so they hold at unlimited
    budget too. It matters ONLY with `MTG_SEA_GORGER_ALL=0` or in another deck. Does the USER accept it, or want the
    full keep-j subset product (size blows up with the land classes)? (b) The draft's cheaper ~10-entry default-OFF
    lever was DROPPED: the default is a one-option prune, and the =0 arm already gives the entry census. (c) "Search
    and exile 0" (a 0/0 that dies) is pruned in the ROOT provider (offered to a human; `MTG_UNPRUNE=gorgerzero`), not
    in the engine. (d) The new param is NOT in CardDatabase's hand-gain / walk-bound audited whitelists (conservative,
    as Sin in Q18(d)). (e) Q11: the bounce reset is fixed for Gorger only (`RevealedCard` gated on Gorger's param);
    Figure of Destiny is untouched, so Q11 stands. (f) NOT fixed: the cast-order projection rates a Gorger in hand as
    its printed 6/6, not as an N/N (cast order is USER-owned). The value leaf has no lands-in-library feature, so a
    future leaf cannot see what the exile is worth. (g) Early signal, not the Stage 5 A/B: at d3 b10 on 40 games only 4
    Gorgers resolved, with 12-16 library lands each. The =0 searched arm picked `exile_all` 4/4. It was digest-identical
    to the prune, with +7.8% units. The <6-lands decline never fired.

## Stage 3 / 4 / 4-bis (2026-11-17)

* Stage 3 coverage re-run CLEAN: 0 missing, every card `full`. `fetch_hold_review` is answered by the USER ruling
  (no searched hold): Misty is `fetch_hold_human_only`; Heath / Strand ride the root default (no hold) because
  SeaCreaturesProvider does not override ConsiderFetchHold.
* Stage 4 profile `3a21f5afb` (analyze_deck.py --no-rebuild): cost diagnostic COST_NEUTRAL; discard analysis
  STATUS_QUO_OK. `provider_audit` routes Sea -> SeaCreatures (cert NotAssessed), `--check` rc 0.
* 4-bis `b96973a0a`: rows in ALL THREE tiers (Boars' entry shape + sea2hg), backstop blocks 30011 (regression) /
  30051 (overnight) screened from 16 candidates. Suite cost gate PASS 6381.6 ms/game = 0.99x Hinata (same run).
  GT accepted per tier with --deck=sea,sea2hg; only sea keys moved; check_gt_logs clean. Hinata regression
  d5 s2002/s3003 digests differ from GT by one game each (same win turn) on EVERY binary from base 8cd0bbf23 on:
  pre-existing, not accepted here.

## Claude-play sweep
- commit: `3a21f5afb` (play-identical to `b96973a0a`: only suite rows / GT changed since)
- seeds: 8900000..8900015 (one game per seed; `--claude-play` varies by SEED, not game-index) games: 16
- flags: 0 unresolved
- Driver: Claude (Sonnet player agents). Every result was re-validated by replaying its choice CSV at its seed
  (agents' shared `/tmp` helper scripts collided mid-run; all 16 replays reproduce the reported win turn).
- Claude vs search (d5 b200): equal 8, Claude slower 6, Claude unwon 1 (s8900002: one blue source through T8),
  Claude FASTER 1 (s8900009: T6 vs T7). Root cause of s8900009: the search MULLIGANS the 7 Claude kept (Heath,
  Misty, Forest, Breeding Pool, Worn Powerstone, Kiora, Spawning Kraken) and wins T7 off six cards at b20, b200
  and b2000 alike; a keep decision (USER-owned, the later mulligan stage), not a play defect. Three of the slower
  games are the reverse (Claude kept what the search mulliganed).
- Verified correct in play by the players: land-aura yields (Sprawl / Wild Growth / Overgrowth stacking), shock
  and fetch life, Hedge Maze surveil after the shuffle, Worn Powerstone tapped, Koma's four Coils per connection,
  Spawning Kraken per-connection 9/9s (Koma / Coils / Sin count), Kiora I / II / III + sacrifice, Sin stripping
  Kiora's lore and Kiora re-firing chapter I, Summon: Leviathan's attack draws and its chapter-III sacrifice before
  combat, Hullbreaker / Brinelin prompts (declined), the cleanup discard.

## Stage 5 A/B of the USER prunes (2026-11-17)

One pooled batch, per-job HeuristicArm flags, d5 b20 play settings, 4 x 100 games on held-out seeds
8810001..8840001, `build/scratch/sea/ab5/`. Paired per game vs the default arm (loss = 9).

| arm (flag) | faster / slower | mean delta | units | verdict |
|---|---|---|---|---|
| fetch Forest-first (`MTG_SEA_FETCH_ISLAND_FIRST=0`) | 0 / 0 | 0 | -0.0% | flag live, almost never binds |
| fetch prune OFF (`MTG_SEA_FETCH_PRUNE=0`) | 11 / 0 | -0.0325 | +70.9% | ALL 11 CLAIRVOYANT -> keep the USER rule |
| Sprawl {U,G} searched (`MTG_SEA_SPRAWL_BLUE=0`) | 0 / 0 | 0 | +23.9% | prune is free |
| bounces searched (Hullbreaker + Brinelin =0) | 0 / 0 | 0 | +51.1% | prune is free |
| Wave searched (`MTG_SEA_WAVE_NEVER=0`) | 0 / 0 | 0 | +8.0% | prune is free (lines differ, turns equal) |
| Sin full menu (`MTG_SEA_SIN_STRIP_SAGAS=0`) | 0 / 0 | 0 | +0.1% | prune is free |
| Gorger full menu (`MTG_SEA_GORGER_ALL=0`) | 0 / 1 | +0.0025 | +7.7% | the 1 slower game is a budget effect (identical at b40+) |
| main 2 every turn (`MTG_FORCE_USES_M2=1`) | 11 / 0 | -0.0275 | +12.9% | exposes a MAIN-1 ENGINE DEFECT (below) |

Root cause: `logs/sea-research/ab-rootcause.md` (45 single-game replays, all reproduce).
* **Fetch:** the per-card reshuffle is visible to the search, so the target choice picks which draw disappears
  (top-snipe 5, deep fetch 4, Hedge Maze surveil of a known top land 2). Blind search: 24 / 20, net -0.011 (noise).
  No Gorger land-count, Enchant-Forest, shock-life or colour effect. Keep the USER's Breeding Pool rule. Possible
  non-clairvoyant residue: Hedge Maze's surveil ({Pool, Maze} searched pair) -- needs a blind A/B first; not built.
* **Main 2:** no combat resource in any of the 11 games. Each is "cast Utopia Sprawl / Wild Growth in main 1, then
  Worn Powerstone (7) / Overgrowth (4) off the enchanted land", which main 2 reaches and main 1 does not -- main 1
  (a) does not ENUMERATE the line (gi39, gi75) or (b) enumerates it and never CHOOSES it even unbudgeted (gi64).
  Budget-insensitive (2x / 4x / 8x); 6 of 11 survive blind search. An ENGINE fix is in flight on branch
  `sea-landaura-fix` (worktree `.claude/worktrees/sea-landaura`), A/B on every land-aura deck. The main-2 shape (Q12)
  stays the USER's call; re-measure every-turn vs conditional after the fix.
* **Gorger gi57 (s8840001):** arms split on turn 1 before any Gorger exists; at b20 the extra menu re-prices the
  lookahead; b40 / b80 / b160 play identically. Uninformative about the Gorger choice.

## Stage 5c2 leaf tie-break / 5i discard bound (2026-11-17, re-run after the 9p loss)

* **5c2 leaf tie-break** (`scripts/leaf_tiebreak_check.py --tag sea`, 12000 paired games at play settings, one pooled
  batch): net -3 turns, 0 worse / 2 better, binding in 2 of 12000 (0.017%). The script's verdict is "no sign at this
  sample"; at 12000 pairs it is effectively unbindable for this deck, so the default (ON) stands -- a lever that
  almost never fires costs nothing. No provider opt-out proposed.
* **5i discard best-vs-worst bound** (`build/scratch/sea/discbound/`, `MTG_SHED_WORST` + `MTG_NONCLEANUP_SHED_WORST`
  vs the search's choice, 200 g per cell, seeds 8850001 / 8860001):

  | cell | search shed | worst shed |
  |---|---|---|
  | d3 b10 s8850001 | 5.910 | 5.910 |
  | d3 b10 s8860001 | 5.880 | 5.885 |
  | d5 b20 s8850001 | 5.900 | 5.895 |
  | d5 b20 s8860001 | 5.870 | 5.870 |

  The bound is ~0 (+/-1 game-turn in 200): the discard choice does not move this deck. 20 real sheds in 1600 games.
  The bucket ladder proposal (`docs/design/sea-creatures-discard-policy-proposal.md`) is therefore a correctness /
  readability item for the USER, not a win-turn lever.
* **5a verify_deck viewer gate:** the auditor read Koma / Brinelin `legend_keep` as a HARD MISS. Expectation bug in the
  audit (a single cast forces nothing; the prompt needs a second, differing copy beside the first); fixed to
  NOT_FORCED in `scripts/audit_viewer_decisions.py`.

## Approved deferrals

(none yet)
