# Analysis ledger — AccomplishedAlchemist

Deck: `decks/AccomplishedAlchemist/AccomplishedAlchemist.cod` (mono-green + Living Wish sideboard).
Started 2026-10-18 on branch `accomplished-alchemist` (worktree `.claude/worktrees/accomplished-alchemist`,
forked from `phase-1-2-deck-analyzer` @ 2176032d).

## RESUME POINT

Stage 2 (card research fan-out) in progress.

## The deck

Main (60): 3 Accomplished Alchemist, 4 Living Wish, 4 Crab Umbra, 4 Utopia Sprawl, 4 Stroke of Genius,
18 Forest, 4 Nourish, 4 Staff of Domination, 4 Elvish Harbinger, 1 Ezuri Renegade Leader,
1 Seedcradle Witch, 3 Arbor Elf, 1 Wild Growth, 1 Weather the Storm, 4 Feed the Clan.

Side (15, ALL reachable via Living Wish — creature or land): 1 Accomplished Alchemist, 2 Galvanic Alchemist,
1 Trufflesnout, 2 Scholar of Athreos, 1 Forest, 1 Island, 1 Eternal Witness, 1 Arbor Elf, 1 Brindle Boar.

### Intended engine (to be confirmed against Scryfall oracle text, never recall)

Accomplished Alchemist taps for X = life gained this turn (one colour). Lifegain (Nourish, Feed the Clan,
Weather the Storm, Staff of Domination's lifegain, sideboard lifegain creatures) pumps X. An untapper
(Crab Umbra on the Alchemist, Staff of Domination's untap-creature ability, Galvanic Alchemist) turns
X >= the untap cost into unbounded mana. Payoffs: Stroke of Genius aimed at the OPPONENT (deck them,
the opponent has a 53-card library when `opponent_library_dealt`), Ezuri's repeatable Elf pump,
Staff's draw. Elvish Harbinger tutors an Elf (Alchemist / Ezuri) to the top.

## Decisions carried into every subagent (do not re-litigate)

* Stroke of Genius's existing note ("target player = self") is WRONG now that the opponent has a library —
  `docs/design/passive-opponent-no-library.md` says so explicitly. Targeting the opponent is the deck's kill.
  Any forced-draw-on-opponent param must be added to `GoldFishRunner::DeckTouchesOpponentZones`.
* Drawing from an empty library loses at the next SBA check (CR 704.5b) — not at the next draw step.
* Everything faithful; a deferral is PROVISIONAL until the user signs it off.

## Cards (Stage 2)

| card | board | status | tier | notes |
|---|---|---|---|---|
| Nourish | main | implemented (chunk A) | 1 | cast_lifegain 6 (ApplyCastLifegain); no viewer decision; opponent's-turn cast PROVISIONAL inert |
| Eternal Witness | side | implemented (chunk A) | 1 | etb_return_gy_to_hand (Timeless Witness); viewer: dig (bucket A); PROVISIONAL 'may'->mandatory |
| Staff of Domination | main | drafted | 2+3 | see logs/aa_drafts; self_untap NEW; untap target not searched (viewer bucket B) |
| Crab Umbra | main | drafted | 3 | BLUE {U}, untap {2}{U}; NEW untap_enchanted_cost + UntapEnchanted action |
| Trufflesnout | side | drafted | 3 | NEW searched ETB mode axis + etb_mode viewer decision |
| Scholar of Athreos | side | implemented (chunk A) | 2 | drain params + NEW drain_each_opponent (x heads on loss AND gain; DrainLossPerActivation / DrainGainPerActivation at every drain site); lethal drain COUNT needs the chunk-C provider (ManaSinkActivationCounts) |
| Weather the Storm | main | implemented (chunk A) | 2 | cast_lifegain 3 + NEW storm bool (one GainLife per instance); ProjectPlanWalk now counts the plan's casts; cast order = USER decision (generic kept, storm-last only proposed) |
| Ezuri, Renegade Leader | main | implemented, fair pump (chunk A) | 1 | team_pump_* +3/+3 Elves (combat converter); main-phase lethal-K pump PENDING chunk C; regenerate/trample PROVISIONAL inert |
| Galvanic Alchemist | side | drafted | 3 | soulbond + NEW granted untap {2}{U}; partner pick greedy today |
| Elvish Harbinger | main | implemented (chunk A) | 1 | Giant Harbinger tutor_to_top Elf + 5c dork; tutor axis AND the 'may' decline are searched variants (tutor_optional) -- no deferral needed |
| Seedcradle Witch | main | drafted | 3 | NEW untap+pump targeted activation {2}{G}{W} |
| Utopia Sprawl | main | drafted | 2/3 | Enchant Forest (NEW host subtype) + locked colour (searched?) |
| Brindle Boar | side | implemented (chunk A) | 2 | self-sac outlet + NEW sac_outlet_lifegain 4; gate = SelfSacOutletUseful (death payoff OR read lifegain); self-only victim now baked correctly (CanonicalSacVictim self_only) |
| Stroke of Genius | main | implemented (chunk B) | 3 | NEW draw_target_player (+ targeting player): searched (target, X) cast variants via DrawXCandidates (opp = minimal lethal X only; self = cap/half/1..3 capped at library; human = all X); opponentdeck::ForcedDraw deck-out (decked at n >= library); shared PerformDrawX (executor ResolveDrawX + rollout); self = plain cantrip (site 3, IsSelfDrawXCast); DeckTouchesOpponentZones trait; fixtures aa_stroke_decks_opponent / aa_stroke_short_of_lethal. OPEN (user): the opponent prune drops non-lethal multi-turn deck-outs |

## Chunk B (2026-10-18) -- landed: Stroke of Genius

* `draw_target_player` (bool, new) + `targeting: player`. The TARGET PLAYER and X are both searched cast
  variants (`Action::draw_target` 0 self / 1 opponent; CastRiders -> rollout record + executor Player target).
  Every signature folds both (`#DX<x>/<t>` in the autonomous dedup; plan hash / fingerprint / PlansEqual /
  human sig fold draw_target only when nonzero, so no other deck's key moves).
* `opponentdeck::ForcedDraw(state, n)`: decked when the draw EMPTIES their library (n == L) or over-asks it
  (n > L, the CR 704.5b loss). n == L is flagged too, on TakeFromTop's argument: their simulated end-of-turn
  draw then fails on THIS turn, so the reported win turn is identical and only the within-turn win checks
  differ (they now see it). Minimal lethal X = max(1, L) (`ForcedDrawLethalX`). X = 0 decks nobody.
* `PerformDrawX` (SpellEffects.h) is the ONE resolver for both worlds; a self over-draw sets
  `player_lost_on_draw` (the old ResolveDrawX drew what was left and carried on).
* `DecisionProvider::DrawXCandidates` (new hook, default in DecisionProviders.cpp): opponent = the minimal
  lethal X when affordable, else nothing; self = {cap, ceil(cap/2), 1, 2, 3} with cap = min(max, own
  library); MTG_UNPRUNED=xspell opens 1..max; human play gets 0..max for both targets.
  **USER QUESTION (surfaced, default taken):** a non-lethal forced draw is a no-op THIS turn but not across
  turns (4 Strokes + a large finite pool can split a deck-out over two turns). Default = prune it (the brief's
  "dominated" reading); MTG_UNPRUNED=xspell is the A/B arm.
* Self-draw X is a PLAIN CANTRIP for the breakpoint machinery (site 3) via `IsSelfDrawXCast` -- the rollout's
  DrawSpell branch, IsPlainCantrip, BpSiteMask, the executor's equip_bp_truncates and cand_draws all read it;
  the opponent variant opens nothing. `ParamKeyedDrawClass` now claims DrawX (so site 10 never double-arms).
* Viewer: a `main_phase` plan variant labelled "(X=n) -> you draw" / "-> opponent draws", plan JSON
  `"draw_target"`; DECISIONS.md row + `draw_target_player` -> main_phase in audit_viewer_decisions.py.
  No resolution-time board click (the target fixes X's meaning, so it is chosen with the plan).
* Braingeyser / Unite the Coalition notes reworded to name the limitation (FiveColour deals no opponent
  library), param NOT added.
* Mana: the AA fixture pays {6}{U} with the Alchemist's one blue through the exact payer. The pre-combat
  pool credits the Alchemist exactly 1 (no lifegain-growth credit), so a Nourish -> Alchemist -> big-X Stroke
  line in ONE main is not sized by the enumerator yet -- chunk C.

## Chunk A (2026-10-18) -- landed

Nourish, Eternal Witness, Elvish Harbinger, Ezuri (fair pump), Scholar of Athreos, Brindle Boar,
Weather the Storm. New params: `drain_each_opponent`, `sac_outlet_lifegain`, `storm` (tests:
test/unit/test_accomplished_alchemist.cpp + F4c in test_attack_projection.cpp). Also:
`deck_reads_lifegain_in_play` now scans a wish-reachable sideboard; ProjectPlanWalk counts the plan's
casts (storm); stale "MTG_SL_SECOND_MAIN default ON" notes corrected (Alchemist, Feed the Clan,
Bogbeast, HeuristicArm.h) -- the default stays OFF. Separate commit: a self-only sac outlet now bakes
itself as the victim (was a silent no-op) -- moves critter_smoke_d0 4 digests, win turns unchanged;
smoke GT for that NOT accepted yet. ROUTING measured: the deck rides SelesnyaLifegainProvider today
(pinned in test_pirates_provider.cpp until chunk C's own provider).

## Open risks

* ROUTING: mana_per_life_gained + cast_lifegain_ferocious are terms in the selesnya signature (DecisionProviders.cpp ~21151) -> deck likely rides SelesnyaLifegainProvider. Stage 4a must give it its own provider derived from what it routes to today.

* ROUTING (2): Arbor Elf's mana_requires_land_subtype sets the stompy signature (checked before anti) -> may ride StompyProvider. tutor_to_top -> anti (TutorSearchWidth 2). Own provider, width >= 5.
* Alchemist bracket note 'no cost asks for two different colours' is FALSE in this deck (Seedcradle Witch {2}{G}{W}); also blue (Umbra/Galvanic/Stroke), black (Scholar).
* Utopia Sprawl colour + Galvanic soulbond partner are provider picks today = greedy-in-window concern; prefer searched axes.
* SEQUENCING (central): Alchemist's bracket note says DeckUsesSecondMain is ON for Alchemist decks, but MTG_SL_SECOND_MAIN went DEFAULT OFF 2026-09-30 (measured on SelesnyaLifegain, GoldFishRunner.cpp:234). Sac-outlet / tap-lifegain activations are TRAILING (after casts) and Alchemist is credited 1 pre-gain -> same-main lifegain->Alchemist conversion likely unreachable. Architecture design must settle this; the m2 lever for THIS deck is a USER-visible decision (measure both).

## Viewer decisions (2c-ter)

## Verification (Stage 5)

## Open questions surfaced to USER (defaults taken)

* Stroke at opponent offered only at minimal lethal X (chunk B). Lossy vs completeness (a 2-turn split deck-out). Default taken: chunk C2 adds a max-affordable opponent variant too; MTG_UNPRUNED=xspell is the full arm.
* Second main for this deck: global default OFF kept; m2 arm to be measured.
* Weather the Storm cast order: generic kept; storm-last proposed.
* Utopia Sprawl colour / Galvanic partner: made SEARCHED (not provider picks).
* critter_smoke_d0_s1001 moves 4 digests (avg unchanged) from the chunk-A self-only sac-outlet victim fix -> accept at landing.

## Approved deferrals

(none yet)

## PROVISIONAL deferrals (awaiting user sign-off)

* Eternal Witness: 'you may return' collapsed to mandatory (returning is weakly dominant; no graveyard-count/discard payoff in deck).
* Staff of Domination: {4},{T}: Tap target creature -- inert (opponent creatures never attack/block; nothing reads tapped state; tapping own creature is never better).
* Ezuri: regenerate inert (nothing destroys our creatures); trample inert (no blockers).
* Crab Umbra: Umbra armor inert (nothing destroys our creatures).
* Instant-speed activity on the OPPONENT's turn (Crab Umbra untap, Nourish) not modelled -- engine acts in our phases; an end-step kill lands strictly later than the same line in our main.
