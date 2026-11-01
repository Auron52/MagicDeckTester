# Analysis ledger — Merfolk

Deck: `decks/Merfolk/Merfolk.cod` (60 cards, no sideboard). Analysis opened 2026-11-01 UTC.

## Standing constraints

- The repository's `CLAUDE.md`, `.claude/skills/analyze-deck.md`, MTG rules, and MTG AI guidance apply.
- The user reports a mulligan-profile run in another container. Finish research and implementation first; notify the user before CPU-heavy builds or simulation batches.
- The new deck needs its own provider before measurement. Search narrowing belongs only in a reviewed provider. Cast order, discard buckets, and other user-owned decisions remain for user review.
- No new exhaustive mulligan profile is part of this analysis; Stage 4 generates a baseline profile only.
- User instruction during integration: Phantasmal Image should try to copy a Merfolk lord when available, preferring a 2/2 lord. Add a Merfolk-scoped provider heuristic after the card set is implemented, then measure it against the unmodified candidate order/full search before shipping.

`MerfolkProvider::CopyCreatureCandidates` stably orders legal Image copy targets as own printed 2/2 Merfolk lords, other own Merfolk lords, then other creatures. It does not remove search or viewer choices. Search and resolution integration are complete; outcome measurement is pending.
`MTG_MERFOLK_IMAGE_LORD_FIRST=0` restores the ordinary target order for an A/B check; default is on to follow the user's requested preference.

## Stage 1 — coverage

`python3 scripts/analyze_deck.py decks/Merfolk/Merfolk.cod --coverage-only` reported 11 missing cards: Lord of Atlantis, Master of the Pearl Trident, Vodalian Hexcatcher, Sink into Stupor, Silvergill Adept, Phantasmal Image, Tide Shaper, Cosi's Trickster, Spreading Seas, Kumena's Speaker, Merfolk Mistbinder. Island, Aether Vial, Botanical Sanctum, Unclaimed Territory, Secluded Courtyard, and Waterlogged Grove are already present. No sideboard is reachable. `test/ref_bench.json` was already modified before this work and is user-owned.

## Stage 2 — live Oracle research and integration

Three research agents fetched Scryfall's live card records. One integrator owns all shared C++ and `cards.json` writes.
The static and runtime passes report 17/17 card definitions, zero missing, and no unmodeled viewer choices. A targeted live Scryfall cost audit compared all 11 newly added costed cards and found 11/11 mana costs correct.
The Scryfall field snapshot was refreshed for those 11 cards; the targeted field audit passed all hard fields (cost, P/T, types, keywords). Its six Oracle-text advisories are the explicit bracket notes on Lord of Atlantis, Master of the Pearl Trident, Vodalian Hexcatcher, Phantasmal Image, Cosi's Trickster, and Sink into Stupor.

Stage-2 integration and read-only review are complete. Silvergill's optional `{3}` is represented as a synthesized paid cast face while the printed hand card remains MV 2 for Aether Vial; Image copy targets remain all legal search/viewer choices; Tide and Seas use a live Island replacement that reaches payer and subtype checks; Sink bounces an opponent nonland permanent. `git diff --check`, JSON parse, viewer audit, viewer JavaScript syntax, build, and the unit suite passed.
Review initially suspected a Tide Shaper target gap when a land is played earlier in the same plan. Source tracing resolved that concern for this deck: `EnumeratePlansWithLandUncached` plays the selected land into a copied state before enumerating spell actions, so both Tide and Spreading Seas see the new land and its stable ID. A separate review found Sink's target legality missed hexproof; the shared search, resolution, viewer, and free-cast paths now check target legality.

| Card group | Important implementation work |
|---|---|
| Four Merfolk lords | Other-Merfolk bonuses are modeled. Lord of Atlantis's symmetric grant, islandwalk, Hexcatcher's flash and sacrifice-to-counter ability are provisionally inert under this opponent. |
| Silvergill Adept | Reveal another Merfolk or pay {3} as an additional cast cost; ETB draw. Vial bypasses cast cost. |
| Phantasmal Image | Choose any creature to copy, preserve its Illusion type, or decline the copy. Its sacrifice-on-target trigger is provisionally inert for this list. |
| Tide Shaper | Kicker {1}, kicked land target and lasting Island effect, bonus while an opponent controls an Island. |
| Cosi's Trickster | Its opponent-shuffle trigger is provisionally inert because the opponent shuffles only before permanents exist. |
| Kumena's Speaker | +1/+1 while another Merfolk or an Island is controlled. |
| Spreading Seas | Draw on ETB; enchanted land becomes an Island, losing old land subtypes and printed abilities while attached. |
| Sink into Stupor // Soporific Springs | MDFC land's pay-3-life or enter-tapped choice; front targets a live opponent nonland permanent and bounces it. Spell targeting is provisionally inert because the passive opponent never casts. |

### Provisional questions and modeling decisions

- Islandwalk does not change combat against this passive opponent because it never blocks. Any omitted islandwalk grant must be marked **PROVISIONAL** and shown to the user for sign-off.
- Cosi's Trickster appears unable to see an in-game opponent shuffle under the passive opponent model. A skipped trigger must be marked **PROVISIONAL**, with the exact reachability proof recorded.
- Vodalian Hexcatcher's counter ability needs care: the opponent does not cast, but targeting one's own noncreature spell to sacrifice a Merfolk is theoretically legal. Check whether that can change this deck's result before any **PROVISIONAL** simplification.
- The opponent does have spawned creatures in ordinary games; it is incorrect to assume its battlefield is empty. Recheck every targeting and land-type simplification against that setup.

## Remaining gates

Horizon tie-break verdict and final assumptions disclosure remain pending. Static Stage 3 coverage is 17/17 full, with zero missing or partial cards. The offline field audit reports 658 checked definitions, zero hard mismatches. All 2,278 unit cases pass after the land-link zero-sentinel fix and the explicit Merfolk provider pin.

## Stage 4 — baseline and provider

`analyze_deck.py --no-rebuild --analyzer-seed 1001` generated `Merfolk.profile.json` with 17 card scores and static/default mulligan settings, without an exhaustive keep or bottom table. The cost diagnostic reported `NO_COST_INTERACTIONS`. `provider_audit.py --check` routed Merfolk to `MerfolkProvider`.
The discard evidence run saw zero real cleanup discards in 400 games but 6,097 rollout sheds, so its verdict was `DISCARD_UNLABELLED`. A paired 150-game-per-arm bound at d3/b10 and d5/b20 gave identical averages and play digests for default versus `MTG_SHED_WORST=1` (d3 4.4067, d5 4.3933). This was the pre-bucket baseline.

### User-directed Merfolk cleanup buckets (2026-11-01)

The user supplied the rule after that baseline: retain at least one one-drop and three lords or Phantasmal Images when available. Keep at most three total land/Vial sources until two are on the battlefield, then up to four total; with two down, that leaves five creature slots in a seven-card hand. Count battlefield cards first. A Vial in hand fills a source slot only at turn-one cleanup when no Vial is already down. Sink into Stupor counts as a land only when ordinary lands do not fill the source target; otherwise it sheds before other cards. Spreading Seas is retained only after all creatures and source needs, never over a creature. Within the lord group, Image improves when an existing lord can be copied, then printed 2/2 lords. The exact ranking is in `MerfolkProvider::CleanupDiscardShedOrder`; `MTG_MERFOLK_BUCKET_DISCARD=0` restores generic discard. A targeted unit test passes all 18 assertions, including the Sink MDFC route, Seas below creatures, the board-netted source quota, and turn-one Vial.

A paired 200-game-per-cell A/B at seed 570000 gave identical win turns and play digests at d3/b10 (4.4150, digest `146ef1934bed2e8a`) and d5/b20 (4.4150, digest `2167221ef93b4ed4`) for bucket on versus generic. Changed games: zero of 400. Full unit and smoke/regression replay are in progress.

## Suite admission and measured play

The same-run `suite_gate.py --cost` comparison passed: Merfolk 1,804.040 searched core-ms/game against Hinata 4,388.507, a 0.41× ratio under the 3× limit. Provisional pre-keep-table ENTRY rows were admitted in smoke, regression, and overnight; smoke also has a 50-game 2HG d3 canary. All three tiers passed, and their Merfolk ground-truth rows and per-game wins were accepted with the same deck filters as their runs. The regression run's strict saved-reference replay passed.
At seed 1001, smoke averages were d0 4.6940, d3 4.4400, d5 4.4200; 2HG d3 averaged 5.0200. Overnight play d5 averages across seeds 4004/5005/6006/7007 were 4.3500/4.3500/4.4033/4.3833. Across all 1,600 admitted d5 games, Merfolk won 1,600/1,600 with a 4.3756 average turn against the passive opponent. A paired independent 200-game seed block gave d0 4.6800, d3 4.3800, d5 4.3800.

## Image preference A/B

The user-requested provider orders own printed 2/2 Merfolk lords first, then other own Merfolk lords, without deleting any legal Image target. With `MTG_MERFOLK_IMAGE_LORD_FIRST=0`, ordinary battlefield order is restored. In the paired 200-game seed-450000 block, lord-first averaged 4.3800 and ordinary order 4.4950 at **both** d3/b10 and d5/b20. Lord-first won a turn earlier in 23 games, later in zero. A reproduced seed-450021 game showed Image copying Merfolk Mistbinder on turn 3 and winning turn 4 with the preference; ordinary order left Image in hand and won turn 5. The preference remains default on.

## Stage 5 — runtime verification

`verify_deck.py decks/Merfolk/Merfolk.cod --no-network` passes coverage, card fields, all three regression tiers, the runtime viewer surface and its three decision types (`land_entry`, `target`, `vial_charge`), mismatch harnesses (no nonconv or fd-diverge on seed bases 7001/8001 × 60 games per arm), protocol invariants (8 games, 152 decisions), and suite admission. The cost gate is marked SKIP only because this invocation used `--no-network`; the targeted live Scryfall cost audit already checked all 11 new costed cards 11/11 and found no mismatch.
The first viewer sweep reported a Sink target hard miss. This was an auditor false positive: its summary substring test counted `land=Sink into Stupor -> Soporific Springs` as casting Sink's targeted front. The auditor now counts actual chosen cast actions and recognizes the two MDFC routes separately. A targeted 40-game seed-search verified Springs' `land_entry` in game index 2 and Sink's spell `target` in game index 18; the full runtime viewer sweep then passed.
The paired 200-game depth block gives d0 4.6800, d3/b10 4.3800, d5/b20 4.3800. The only turn-7 win in that block was seed 450068/game index 8: two mulligans left a one-land keep, the second land arrived on turn 4, and a tenfold search budget (b200) still won turn 7. This is an opening-hand constraint, not play-budget starvation.

## Claude-play sweep

One independent Claude/Sonnet game (seed 470000) completed and matched the search's turn-4 win. It found no legality or state-delta issue; its one command warning came from an obsolete explicit `--lookahead-bottoming` CLI flag, which is automatic at searched depth. The other 14 distinct-seed agents could not play: the Claude CLI returned its weekly usage limit. **UN-RUN remainder: 14/15 games.** Do not record `flags: 0 unresolved` or treat the sweep as passed. Re-run the remaining Claude oracle games when the external limit resets; `verify_deck.py` intentionally reports `claude_sweep: SKIP` until then. No Codex model was substituted for Claude.

## Approved deferrals

None. Provisional notes above are not approvals.
