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
  **USER APPROVED 2026-11-16, with Breeding Pool fetched first (see Open USER questions #1).**
* Every creature in the list is a Kraken / Leviathan / Octopus / Serpent, so the "except sea creatures"
  bounces (Whelming Wave, Summon: Leviathan I) only ever hit the opponent's spawned creatures, which
  never block or attack: faithful but payoff ~0 in goldfish.

## Stage 2 — research drafts

| draft | cards | file | status |
|---|---|---|---|
| combat | Koma, Spawning Kraken, Serpent, Lorthos, Whelming Wave | logs/sea-research/combat.md | DONE: Spawning Kraken Tier 1 (Millicent per-creature `FireCombatDamageTokens`, shared executor/rollout); Koma Tier 2 (`combat_damage_watch_self` + optional token-name param "Koma's Coil"); Serpent Tier 1 + inert-evasion note; Lorthos Tier 1 + dominance fold (always decline {8}); Whelming Wave Tier 2 (bounce helper shared with Summon I, + Merfolk). No second-main need. Proposed provider fold: skip casting Wave unless Hullbreaker on battlefield/in hand (default ON, `MTG_UNPRUNE=seawave`) -> USER Q. PROVISIONAL deferrals: Serpent evasion (Master of the Pearl Trident precedent, USER-approved 2026-11-01), Lorthos tap/freeze. Cross-notes: Kiora token subtype Kraken; stolen creature keeps opponent owner. |
| sin | Sin, Unending Cataclysm | logs/sea-research/sin.md | DONE: Tier 3 medium-small, no new struct fields (`Permanent::lore_counters` exists). Shared removal helper at `FireEtbWatchers` (every entry route), 2x as +1/+1. Choice = live-probe subset pin (`@H?` -> `@H<n>,<n>`, the Life-from-the-Loam probe), fold identical copies to k-of-m, no lossy prune; rollout default harvest every Saga. Viewer bucket A (`dragon` multi-pick). Risk: pin read only by Sin's own entry; probe kept out of the TT. Constraint for sagas: never assume lore>=1; a reset Saga re-fires chapter I. PROVISIONAL deferral: dies trigger (Sin cannot die in this list). |
| gorger | Trench Gorger | logs/sea-research/gorger.md | DONE: Tier 3, no deferral. Param `etb_exile_library_lands_set_base_pt`, shared resolver beside Primeval Titan's ETB. Decline = 6/6 no shuffle; search always shuffles; exile 0 -> 0/0 dies (pruned for AI, offered to human). Branch on the in-plan land-search pin: provider-ordered keep menu (~54 entries, <62 cap): exile-all (default + rollout), keep one of each kind, every keep-two, then searched count N with provider-chosen lands for N>=3 (DISCLOSED LOSSY, Genesis Wave precedent -> USER Q), + decline. Cheaper ~10-entry menu drafted as default-OFF lever. Per-card reshuffle keeps other cards' order (thinning valued on the seed's real draws). Viewer bucket A (dig yes/no + dragon multi-pick). Risks: bounced Gorger keeps N/N in hand (fix for Gorger; Figure of Destiny latent same bug -> USER Q, moves GT); sim/dominance keys need power folds. |
| bounce | Hullbreaker Horror, Brinelin | logs/sea-research/bounce.md | DONE: Tier 3. Shared `ReturnPermanentToOwnersHand` (also Whelming Wave / Summon I); cast triggers in `FireOnCastTriggers`; Brinelin ETB deferred past the enter cascade; per-instance searched target digit (Raggadragga machinery, ordinal renamed/shared), decline included; viewer `target` with min_targets 0. RISK: width (~20k combos late) -> measure at 5f, waves not caps. PROVISIONAL deferrals: Hullbreaker Flash (main-phase cast dominates), Brinelin Partner. Open: tap-in-response Sol Ring float; DeckUsesSecondMain for Hullbreaker post-combat re-buy. |

## Integration (serial)

| step | change set | commit | status |
|---|---|---|---|

## Open USER questions (surfaced, not blocking)

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
   Follow-up Q (agent default PROVISIONAL): Brinelin's bounce is the same ability but the USER named only
   Hullbreaker, so Brinelin's trigger stays SEARCHED until the USER says otherwise.
7. Hullbreaker post-combat bounce-and-recast: covered by the deck's second main (Summon draws turn it on).
8. Tap-Sol-Ring-in-response-then-bounce: not expressible today; follow-up, not built now.
10. Trench Gorger exile menu: the N>=3 "which lands" set is provider-chosen, a lossy restriction (all
    21,600 subsets cannot be branched). Agent default: ship the ~54-entry menu, measure which entries
    win, then propose a heuristic prune to the USER. PROVISIONAL.
11. Figure of Destiny latent bug (bounced card keeps its base-P/T override in hand). Fixing it moves
    GT for decks with Figure. Agent default: fix for Gorger now; Figure as a separate USER-approved change.
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

## Approved deferrals

(none yet)
