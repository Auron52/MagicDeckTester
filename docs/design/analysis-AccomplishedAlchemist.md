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
| Nourish | main | drafted | 1 | cast_lifegain 6 (ApplyCastLifegain); no viewer decision |
| Eternal Witness | side | drafted | 1 | etb_return_gy_to_hand (Timeless Witness); viewer: dig (bucket A); PROVISIONAL 'may'->mandatory |
| Staff of Domination | main | drafted | 2+3 | see logs/aa_drafts; self_untap NEW; untap target not searched (viewer bucket B) |
| Crab Umbra | main | drafted | 3 | BLUE {U}, untap {2}{U}; NEW untap_enchanted_cost + UntapEnchanted action |
| Trufflesnout | side | drafted | 3 | NEW searched ETB mode axis + etb_mode viewer decision |
| Scholar of Athreos | side | drafted | 2 | drain params + drain_each_opponent (2HG heads) |
| Weather the Storm | main | drafted | 2 | cast_lifegain 3 + NEW storm bool; cast order = USER decision |
| Ezuri, Renegade Leader | main | drafted | 1+2 | team_pump; infinite-mana payoff needs main-phase lethal-K pump |
| Galvanic Alchemist | side | drafted | 3 | soulbond + NEW granted untap {2}{U}; partner pick greedy today |
| Elvish Harbinger | main | drafted | 1 | Giant Harbinger tutor_to_top Elf + 5c dork |
| Seedcradle Witch | main | drafted | 3 | NEW untap+pump targeted activation {2}{G}{W} |
| Utopia Sprawl | main | drafted | 2/3 | Enchant Forest (NEW host subtype) + locked colour (searched?) |
| Brindle Boar | side | drafted | 2 | self-sac outlet + NEW sac_outlet_lifegain 4 |
| Stroke of Genius | main | drafted (revisit) | 3 | hand-cast {X} DrawX NOT ENUMERATED today; rollout has no DrawX; NEW draw_target_player + ForcedDraw deck-out |

## Open risks

* ROUTING: mana_per_life_gained + cast_lifegain_ferocious are terms in the selesnya signature (DecisionProviders.cpp ~21151) -> deck likely rides SelesnyaLifegainProvider. Stage 4a must give it its own provider derived from what it routes to today.

* ROUTING (2): Arbor Elf's mana_requires_land_subtype sets the stompy signature (checked before anti) -> may ride StompyProvider. tutor_to_top -> anti (TutorSearchWidth 2). Own provider, width >= 5.
* Alchemist bracket note 'no cost asks for two different colours' is FALSE in this deck (Seedcradle Witch {2}{G}{W}); also blue (Umbra/Galvanic/Stroke), black (Scholar).
* Utopia Sprawl colour + Galvanic soulbond partner are provider picks today = greedy-in-window concern; prefer searched axes.
* SEQUENCING (central): Alchemist's bracket note says DeckUsesSecondMain is ON for Alchemist decks, but MTG_SL_SECOND_MAIN went DEFAULT OFF 2026-09-30 (measured on SelesnyaLifegain, GoldFishRunner.cpp:234). Sac-outlet / tap-lifegain activations are TRAILING (after casts) and Alchemist is credited 1 pre-gain -> same-main lifegain->Alchemist conversion likely unreachable. Architecture design must settle this; the m2 lever for THIS deck is a USER-visible decision (measure both).

## Viewer decisions (2c-ter)

## Verification (Stage 5)

## Approved deferrals

(none yet)

## PROVISIONAL deferrals (awaiting user sign-off)

* Eternal Witness: 'you may return' collapsed to mandatory (returning is weakly dominant; no graveyard-count/discard payoff in deck).
* Staff of Domination: {4},{T}: Tap target creature -- inert (opponent creatures never attack/block; nothing reads tapped state; tapping own creature is never better).
* Ezuri: regenerate inert (nothing destroys our creatures); trample inert (no blockers).
* Crab Umbra: Umbra armor inert (nothing destroys our creatures).
* Instant-speed activity on the OPPONENT's turn (Crab Umbra untap, Nourish) not modelled -- engine acts in our phases; an end-step kill lands strictly later than the same line in our main.
