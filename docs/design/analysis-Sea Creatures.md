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
| I1 | mana draft: Worn Powerstone; Flooded Grove + F2 (land-Aura bonus on EVERY greedy filter tap -- the draft's fallback branches 2/3/5 AND the ranked payer's kind-3 free-{C} / kind-2 fed taps, which the draft missed); Hedge Maze + F1 (searched-out look-land surveils AFTER the shuffle: PerformLandSearch, PerformFetch, and also PerformLandTutorToBattlefield (Crop Rotation) + Nissa -8, same defect); SeaCreaturesProvider (fetch prune MTG_SEA_FETCH_PRUNE / _ISLAND_FIRST on the legacy AND battlefield routes, Sprawl MTG_SEA_SPRAWL_BLUE, E1 MTG_SEA_SPRAWL_GREEN_NEED off); Misty Rainforest battlefield model for the HUMAN only (fetch_hold_human_only: autonomous keeps the immediate route, no searched / <=3-life hold) | 4eb5a3e10 (core), 4cf605b4f (Misty, last commit, droppable) | DONE. Units 3377/3377 green. Byte-identity d3 b10 40 g: 4eb5a3e10 vs base = digests+units identical on Mill, Bruna, Boars, Spirits, Treasure Hunt, Creature Giving, Five Colour, Jumbo Cactuar, Accomplished Alchemist, Hinata. 4cf605b4f vs 4eb5a3e10: all identical incl. Five Colour + Sea (also d3 b0) EXCEPT Creature Giving (units, 1/40 play digest gi21, b10 and b0) -- root-caused to CardParams LAYOUT, not Misty: commit 4eb5a3e10's src plus ONE unused bool in CardParams reproduces the exact moved digest (16009bd3, 338192 u); Misty's new code paths are inert there (the old cards.json on the new binary moves identically). Also observed: Creature Giving's d3 b10 result depends on the pooled batch's job mix (same binary). Both pre-existing (latent CG layout/history sensitivity) -> orchestrator/USER. Sea smoke d3 b10 40 g: no crash, 0/40 won (avg 9.0; every creature still missing). Owed: interim provider signature (staple shapes + Kraken watch) -> later steps OR in Sea-only params and drop the staple terms; cost audit rc 0; field snapshot added for the 3 new cards from the local Scryfall JSON (--update was 429-limited); Misty's fetch_hold_review now hold_modelled true. |

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

14. **I1 (agent defaults taken, surfaced):** (a) PROVISIONAL deferral D-1: a FETCHED / searched-out look-land's surveil is the provider's top-disposition rule, not a searched axis (after F1 it is the post-shuffle card; under the prune a fetched Maze is practically unreachable); human play asks. (b) F1 was applied to Crop Rotation's PerformLandTutorToBattlefield and Nissa -8 as well (same rules defect; inert for every current list). (c) The Sea fetch prune keeps the Boars low-life copy (<= 3 life: full held list) and Heath / Strand keep the root <= 3-life autonomous hold -- both unreachable in goldfish Sea (floor 5 life) and both inside the deferred `docs/design/fetch-hold-searched-scope.md` audit. (d) Misty Rainforest: `fetch_hold_human_only` is a new name-free param (the Tarn/Delta shim generalised per card); its `produces` W/U/B/R/G hand proxy is kept (Tarn shape) so no hand reader moves. (e) Creature Giving moves on ANY CardParams layout change (see I1 row) -- a latent determinism-across-builds sensitivity worth its own investigation; it will show up as CG GT churn on every later step that adds a CardParams field.

## Approved deferrals

(none yet)
