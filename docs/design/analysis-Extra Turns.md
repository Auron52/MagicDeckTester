# Analysis ledger -- Extra Turns

Per-deck onboarding verification record (workstream 3 spine). The generated block below is overwritten by `verify_deck.py`; the **Approved deferrals** section is yours to edit and is never touched by the tool.

## Approved deferrals

_Add `- \`gate:key\` -- why this is a genuine, understood deferral (not a bug)` lines here to sign off a pending item. Requires explicit user judgement._

<!-- verify_deck:begin (generated -- do not edit inside) -->
## Last verification (2026-11-07)

`verify_deck.py decks/Extra Turns/Extra Turns.cod --seeds 60001 --games 1 --no-network --write-ledger` -> **FAIL**

| Gate | Status | Blocking | Summary |
|---|---|---|---|
| coverage | PASS | yes | all 15 cards full (missing=0, partial=0) |
| card_costs | SKIP | yes | skipped (--no-network) |
| card_fields | PASS | yes | 712 cards match snapshot (cost/PT/types/keywords); 17 allowlisted divergence(s) |
| clause_ledger | SKIP | no | covered by coverage+bracket-notes+oracle-diff |
| regression_tiers | FAIL | yes | not in the regression suite (no tier) |
| viewer | PASS | yes | self-guard + surface sweep clean |
| viewer_wiring | PASS | yes | 5 type(s) wired (emitter + GUI): flicker, free_cast, sacrifice, target, tutor_etb |
| mismatch | PASS | yes | no nonconv/fd-diverge across seeds [60001] x 1 games (both arms completed) |
| play_invariants | PASS | yes | 1 game(s)/174 decisions: determinism+integrity+progress hold |
| claude_sweep | SKIP | yes | sweep record present but flag count unparseable |
| suite | FAIL | yes | Extra Turns is NOT in test/regression_cases.sh |

### Pending user sign-off (block the gate until fixed OR approved below)
Add a key to `## Approved deferrals` to sign one off (only if it is a genuine, understood deferral -- not a bug):
- `regression_tiers:too_slow` -- absent from all three tiers -- add it to ALL of them (analyze-deck 4-bis), or record the measured cost and get the user's sign-off while it does not fit
- `suite:not_a_case` -- Extra Turns has no regression cases, so no ground truth tracks its play. The value leaf and mulligan profile are BLOCKED until it is added (3x cost rule: scripts/suite_gate.py --cost). See analyze-deck.md Stage 5j.

### Stage 6a disclosure (deferrals + not-yet-built checks)
- coverage deferral -- Extraplanar Lens: lens_imprint_land: optional ETB target is a searched cast variant over distinct own land states, plus decline. The chosen physical land is exiled and its name stored per Lens copy. Each matching land tap gains one additional mana of a type it produced; projected mana, greedy payment, and backtracking payment include the bonus. The bonus is sourced by Lens and is not snow mana.
- coverage deferral -- Emrakul, the Aeons Torn: Can't be countered: cant_be_countered (inert against the passive opponent; its reader is the self-targeted Remand, which may target Emrakul and draws, Emrakul resolving anyway). Cast trigger: cast_trigger_extra_turns 1 -- pushed above the spell at EVERY cast (EffectHandler::PushCastTriggers for hand casts and PushFreeCast's free casts -- cascade, Unpredictable Cyclone -- and the rollout's inline twin in apply_one), so it resolves first, and two casts give two extra turns. An extra turn is a full turn of ours (untap clears summoning sickness, so an Emrakul cast this turn attacks in it; upkeep; draw; a new land drop; mains; combat; end; cleanup); the passive opponent takes no turn before it (no notional upkeep or draw, no scheduled spawn). EXTRA TURNS DO NOT COUNT TOWARD THE TURN COUNT (USER ruling 2026-10-26): GameState::turn_number is the ROUND, so the reported win turn and every search comparison are in rounds; an extra turn costs no search ply (depth counts rounds), so it is always searched inside the window. Flying inert (no blockers). Annihilator 6: the annihilator param -- at declare attackers the defending player sacrifices six permanents, oldest first (the opponent's choice, a fixed rule); LIVE, because the goldfish opponent's scheduled spawns (8 of 10 games) are the Ulamogs' preferred targets. Protection from spells that are one or more colors: NOT modelled, PROVISIONAL inert -- nothing in a goldfish game targets, damages, enchants or blocks Emrakul with a colored spell in the Cyclone list (Remand targets the SPELL on the stack, where a permanent's protection does not function; the Ulamog triggers are colorless abilities); cross-deck caveat: a future deck's colored own-target spell (a pump, Soulfire's own targets, an Aura) would wrongly be allowed to target it. Graveyard trigger: put_into_graveyard_shuffles_graveyard -- a TRIGGER (not Progenitus's replacement): Emrakul reaches the graveyard first, then its owner shuffles the WHOLE graveyard, Emrakul included, into the library (search-salted shuffle), after the whole event that moved it (the cleanup discard, a legend-rule batch); fired from the shared discard hook and every creature death (legend rule, Ulamog the Infinite Gyre's destroy, sacrifice). Exile is not a graveyard (Ulamog the Ceaseless Hunger's exile does not trigger it). Legend rule modelled (keep the oldest: the copies differ only in summoning sickness, and the doomed one fires the same graveyard shuffle).
- coverage deferral -- Ulamog, the Ceaseless Hunger: Cast trigger: cast_trigger_exile_target_permanents 2 -- MANDATORY, fires at every cast (hand casts, free casts); the two targets are DISTINCT (CR 115.3) and chosen as the trigger is put on the stack (CR 603.3d) from EVERY permanent on the battlefield, either side (the spell itself is on the stack, not a target): the goldfish opponent's scheduled spawns when present, otherwise our own permanents (lands, Unpredictable Cyclone, creatures) -- forced. With fewer than two legal permanents the trigger is removed (CR 603.3d). SEARCHED: Plan::cast_trigger_target_choice ranks the options (multisets of identity-folded permanents, two identical Islands being one option) in DecisionProvider::CastTriggerTargetOrder's order -- ORDER only, opponent permanents first; the human picks on the board (the target decision, one pick at a time; a forced pick is not prompted). Re-checked on resolution (CR 608.2b): a target that left is skipped, the other is still exiled. Exile ignores indestructible and is not a graveyard (no Emrakul shuffle); a token exiled ceases to exist. Indestructible modelled (destroy does nothing; -X/-X and 0 toughness still kill). Attack trigger: attack_exile_defender_top 20 -- MANDATORY for each declared attack, before combat damage, via opponentdeck::TakeFromTop; it exiles up to the remaining library size and an emptied passive-opponent library is recognized as a same-turn deck-out win. DeckTouchesOpponentZones deals the opponent a real library whenever this card is present. A creature put onto the battlefield attacking does not fire this trigger (CR 508.4). Legend rule modelled (one copy in the list).
- coverage deferral -- Myriad Landscape: Enters tapped (enters_tapped); taps for {C} (produces [C
- coverage deferral -- Horn of Greed: horn_draw_on_land_play: the shared PlayLandCore fires one draw per Horn for every successful land PLAY, including fetch and a land-play replacement. EnterLand and Myriad search PUTS do not fire it. Draws enter through DrawIntoHand for first-draw miracle timing. The passive opponent plays no lands in this model.
- coverage deferral -- Mind Stone: Mana rock producing {C} (mana_rock + produces). The late-game cash-in rides the existing sacrifice_draw_cost (Fiery Islet / Horizon Canopy), which matches the source by permanent NAME and is not land-gated, so it works unchanged on an artifact: pay {1}, tap, sacrifice to the graveyard, draw one.
- coverage deferral -- Howling Mine: mine_extra_draw: each untapped Mine adds one draw in the controller\'s draw step after the turn-based draw, in both executor and rollout. The passive opponent\'s draw-step twin receives the same count; an empty library loses on the attempted draw. Tapped copies add nothing. The first player\'s skipped first draw step adds nothing.
- card_costs SKIPPED (--no-network) -- Scryfall cost/cmc reality-diff not run
- allowlisted divergence -- Galerider Sliver [keywords]: Keyword-lord: 'Sliver creatures you control have flying' grants flying to your Slivers INCLUDING itself, so the card functionally has flying (modeled
- allowlisted divergence -- Striking Sliver [keywords]: Keyword-lord: grants first strike to your Slivers incl. itself (modeled self-innate). First strike is inert in goldfishing (no blockers). See oracle b
- allowlisted divergence -- Cloudshredder Sliver [keywords]: Keyword-lord: grants flying+haste to your Slivers incl. itself. Flying self-innate + inert in goldfishing; haste additionally granted to other Slivers
- allowlisted divergence -- Haytham Kenway [keywords]: 'Protection from Assassins' is a real keyword but inert in goldfishing (no Assassins in play); the protection-to-other-Knights is an anthem grant, not
- allowlisted divergence -- Goblin Piledriver [keywords]: 'Protection from blue' is a real keyword but inert in goldfishing (the passive opponent has no blue sources or blockers to target); the attack-trigger
- allowlisted divergence -- Progenitus [keywords]: 'Protection from everything' is a real keyword but inert in goldfishing (the passive opponent never targets, blocks, or damages); the graveyard shuffl
- allowlisted divergence -- Bloom Tender [keywords]: Scryfall lists 'vivid' in keywords -- a data quirk (no rules-meaningful innate keyword on this card); the each-color-among-permanents mana ability is
- allowlisted divergence -- Auriok Champion [keywords]: 'Protection from black and from red' is a real keyword but inert in goldfishing on all four DEBT axes (the passive opponent never damages, targets, bl
- allowlisted divergence -- Brightcap Badger [mana_cost]: ADVENTURE CARD (CR 715), authored as TWO entries -- 'Brightcap Badger' (the {3}{G} 3/4 creature face) and 'Fungus Frolic' (the {2}{G} instant half). S
- allowlisted divergence -- Fungus Frolic [mana_cost]: The ADVENTURE HALF of Brightcap Badger // Fungus Frolic -- see the Brightcap Badger entry. Scryfall's combined mana_cost is '{3}{G} // {2}{G}'; the in
- allowlisted divergence -- Fungus Frolic [P/T]: An Instant has no power or toughness. Scryfall reports 3/4 because it describes the COMBINED card, whose creature face is the 3/4 Brightcap Badger. Gi
- allowlisted divergence -- Mycoloth [keywords]: 'Devour 2' is a real keyword and is FULLY MODELLED -- but via the `devour` CardParam, not via Keyword::Devour. The engine's Keyword enum deliberately
- allowlisted divergence -- Brutal Cathar [keywords]: Transforming DFC (Brutal Cathar // Moonrage Brute): Scryfall reports CARD-LEVEL keywords for both faces. The front face's only keyword is Daybound, mo
- allowlisted divergence -- Moonrage Brute [keywords]: Transforming DFC back face: Scryfall's card-level keywords include the front's Daybound and the layout's Transform; Nightbound is modelled structurall
- allowlisted divergence -- Bloodline Keeper [keywords]: Transforming DFC (Bloodline Keeper // Lord of Lineage): Scryfall reports CARD-LEVEL keywords for both faces (Flying, Transform). Transform is the layo
- allowlisted divergence -- Lord of Lineage [keywords]: Transforming DFC back face: Scryfall's card-level keywords include the layout's Transform; the back face carries no transform of its own (transform_fr
- allowlisted divergence -- Emrakul, the Aeons Torn [keywords]: 'Protection from spells that are one or more colors' is a real keyword but INERT here (PROVISIONAL, ledger Q12): nothing in a goldfish game targets, d
- oracle_text advisory -- Gideon, Ally of Zendikar: oracle_text diverges (similarity 0.10); scryfall='+1: Until end of turn, Gideon becomes a 5/5 Human Soldier Ally creature with indestructible that\'s still a pl
- oracle_text advisory -- Basri, Tomorrow's Champion: oracle_text diverges (similarity 0.15); scryfall="{W}, {T}, Exert Basri: Create a 1/1 white Cat creature token with lifelink. (An exerted creature won't untap d
- oracle_text advisory -- Goblin Guide: oracle_text diverges (similarity 0.60); scryfall="Haste
Whenever this creature attacks, defending player reveals the top card of their library. If it's a land
- oracle_text advisory -- Light Up the Stage: oracle_text diverges (similarity 0.69); scryfall='Spectacle {R} (You may cast this spell for its spectacle cost rather than its mana cost if an opponent lost li
- oracle_text advisory -- Crystalline Sliver: oracle_text diverges (similarity 0.61); scryfall="All Slivers have shroud. (They can't be the targets of spells or abilities.)"
- oracle_text advisory -- Galerider Sliver: oracle_text diverges (similarity 0.41); scryfall='Sliver creatures you control have flying.'
- oracle_text advisory -- Striking Sliver: oracle_text diverges (similarity 0.31); scryfall='Sliver creatures you control have first strike. (They deal combat damage before creatures without first strike
- oracle_text advisory -- Cloudshredder Sliver: oracle_text diverges (similarity 0.48); scryfall='Sliver creatures you control have flying and haste.'
- oracle_text advisory -- Hibernation Sliver: oracle_text diverges (similarity 0.49); scryfall='All Slivers have "Pay 2 life: Return this permanent to its owner\'s hand."'
- oracle_text advisory -- Cavern of Souls: oracle_text diverges (similarity 0.40); scryfall="As this land enters, choose a creature type.
{T}: Add {C}.
{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Unclaimed Territory: oracle_text diverges (similarity 0.15); scryfall='As this land enters, choose a creature type.
{T}: Add {C}.
{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Secluded Courtyard: oracle_text diverges (similarity 0.21); scryfall='As this land enters, choose a creature type.
{T}: Add {C}.
{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Mutavault: oracle_text diverges (similarity 0.56); scryfall="{T}: Add {C}.
{1}: This land becomes a 2/2 creature with all creature types until end of turn. It's still a l
- oracle_text advisory -- Aether Vial: oracle_text diverges (similarity 0.35); scryfall='At the beginning of your upkeep, you may put a charge counter on this artifact.
{T}: You may put a creature c
- oracle_text advisory -- Reliquary Tower: oracle_text diverges (similarity 0.44); scryfall='You have no maximum hand size.
{T}: Add {C}.'
- oracle_text advisory -- Dwarven Hold: oracle_text diverges (similarity 0.23); scryfall='This land enters tapped.
You may choose not to untap this land during your untap step.
At the beginning of y
- oracle_text advisory -- Mercadian Bazaar: oracle_text diverges (similarity 0.26); scryfall='This land enters tapped.
{T}: Put a storage counter on this land.
{T}, Remove any number of storage counters
- oracle_text advisory -- Temple of Epiphany: oracle_text diverges (similarity 0.60); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Thundering Falls: oracle_text diverges (similarity 0.63); scryfall='({T}: Add {U} or {R}.)
This land enters tapped.
When this land enters, surveil 1. (Look at the top card of y
- oracle_text advisory -- Land's Edge: oracle_text diverges (similarity 0.51); scryfall='Discard a card: If the discarded card was a land card, this enchantment deals 2 damage to target player or pla
- oracle_text advisory -- Throes of Chaos: oracle_text diverges (similarity 0.06); scryfall='Cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland card tha
- oracle_text advisory -- Tournament Grounds: oracle_text diverges (similarity 0.37); scryfall='{T}: Add {C}.
{T}: Add {R}, {W}, or {B}. Spend this mana only to cast a Knight or Equipment spell.'
- oracle_text advisory -- Dauntless Bodyguard: oracle_text diverges (similarity 0.55); scryfall='As this creature enters, choose another creature you control.
Sacrifice this creature: The chosen creature ga
- oracle_text advisory -- Venerable Knight: oracle_text diverges (similarity 0.52); scryfall='When this creature dies, put a +1/+1 counter on target Knight you control.'
- oracle_text advisory -- Acclaimed Contender: oracle_text diverges (similarity 0.45); scryfall='When this creature enters, if you control another Knight, look at the top five cards of your library. You may
- oracle_text advisory -- Knight Exemplar: oracle_text diverges (similarity 0.27); scryfall='First strike (This creature deals combat damage before creatures without first strike.)
Other Knight creature
- oracle_text advisory -- Valiant Knight: oracle_text diverges (similarity 0.18); scryfall='Other Knights you control get +1/+1.
{3}{W}{W}: Knights you control gain double strike until end of turn.'
- oracle_text advisory -- Kinsbaile Cavalier: oracle_text diverges (similarity 0.05); scryfall='Knight creatures you control have double strike.'
- oracle_text advisory -- Marshal of Zhalfir: oracle_text diverges (similarity 0.49); scryfall='Other Knights you control get +1/+1.
{W}{U}, {T}: Tap another target creature.'
- oracle_text advisory -- Haytham Kenway: oracle_text diverges (similarity 0.53); scryfall='Protection from Assassins
Other Knights you control get +2/+2 and have protection from Assassins.
When Hayth
- oracle_text advisory -- Adeline, Resplendent Cathar: oracle_text diverges (similarity 0.76); scryfall="Vigilance
Adeline's power is equal to the number of creatures you control.
Whenever you attack, for each opp
- oracle_text advisory -- Accorder Paladin: oracle_text diverges (similarity 0.33); scryfall='Battle cry (Whenever this creature attacks, each other attacking creature gets +1/+0 until end of turn.)'
- oracle_text advisory -- Silverblade Paladin: oracle_text diverges (similarity 0.25); scryfall='Soulbond (You may pair this creature with another unpaired creature when either enters. They remain paired for
- oracle_text advisory -- Niv-Mizzet, the Firemind: oracle_text diverges (similarity 0.16); scryfall='Flying
Whenever you draw a card, Niv-Mizzet deals 1 damage to any target.
{T}: Draw a card.'
- oracle_text advisory -- Niv-Mizzet, Parun: oracle_text diverges (similarity 0.23); scryfall="This spell can't be countered.
Flying
Whenever you draw a card, Niv-Mizzet deals 1 damage to any target.
Wh
- oracle_text advisory -- Curiosity: oracle_text diverges (similarity 0.16); scryfall='Enchant creature
Whenever enchanted creature deals damage to an opponent, you may draw a card.'
- oracle_text advisory -- Tandem Lookout: oracle_text diverges (similarity 0.38); scryfall='Soulbond (You may pair this creature with another unpaired creature when either enters. They remain paired for
- oracle_text advisory -- Hero of Bladehold: oracle_text diverges (similarity 0.50); scryfall='Battle cry (Whenever this creature attacks, each other attacking creature gets +1/+0 until end of turn.)
When
- oracle_text advisory -- Windswept Heath: oracle_text diverges (similarity 0.32); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Plains card, put it onto the battlef
- oracle_text advisory -- Marsh Flats: oracle_text diverges (similarity 0.32); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Plains or Swamp card, put it onto the battlefi
- oracle_text advisory -- Bloodstained Mire: oracle_text diverges (similarity 0.32); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Mountain card, put it onto the battle
- oracle_text advisory -- Wooded Foothills: oracle_text diverges (similarity 0.45); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Mountain or Forest card, put it onto the battl
- oracle_text advisory -- Grove of the Burnwillows: oracle_text diverges (similarity 0.20); scryfall='{T}: Add {C}.
{T}: Add {R} or {G}. Each opponent gains 1 life.'
- oracle_text advisory -- Ignoble Hierarch: oracle_text diverges (similarity 0.56); scryfall='Exalted (Whenever a creature you control attacks alone, that creature gets +1/+1 until end of turn.)
{T}: Add
- oracle_text advisory -- Skyshroud Cutter: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 5 life."
- oracle_text advisory -- Plague Drone: oracle_text diverges (similarity 0.70); scryfall='Flying
Rot Fly — If an opponent would gain life, that player loses that much life instead.'
- oracle_text advisory -- Aria of Flame: oracle_text diverges (similarity 0.78); scryfall='When this enchantment enters, each opponent gains 10 life.
Whenever you cast an instant or sorcery spell, put
- oracle_text advisory -- Fiery Justice: oracle_text diverges (similarity 0.54); scryfall='Fiery Justice deals 5 damage divided as you choose among any number of targets. Target opponent gains 5 life.'
- oracle_text advisory -- Swords to Plowshares: oracle_text diverges (similarity 0.17); scryfall='Exile target creature. Its controller gains life equal to its power.'
- oracle_text advisory -- Invigorate: oracle_text diverges (similarity 0.52); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have an opponent gain 3 life.
Target
- oracle_text advisory -- Reverent Silence: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 6 life.

- oracle_text advisory -- Idyllic Tutor: oracle_text diverges (similarity 0.30); scryfall='Search your library for an enchantment card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Enlightened Tutor: oracle_text diverges (similarity 0.32); scryfall='Search your library for an artifact or enchantment card, reveal it, then shuffle and put that card on top.'
- oracle_text advisory -- Forbidden Orchard: oracle_text diverges (similarity 0.22); scryfall='{T}: Add one mana of any color.
Whenever you tap this land for mana, target opponent creates a 1/1 colorless
- oracle_text advisory -- Reflecting Pool: oracle_text diverges (similarity 0.26); scryfall='{T}: Add one mana of any type that a land you control could produce.'
- oracle_text advisory -- Izzet Signet: oracle_text diverges (similarity 0.05); scryfall='{1}, {T}: Add {U}{R}.'
- oracle_text advisory -- Ponder: oracle_text diverges (similarity 0.14); scryfall='Look at the top three cards of your library, then put them back in any order. You may shuffle.
Draw a card.'
- oracle_text advisory -- Preordain: oracle_text diverges (similarity 0.27); scryfall='Scry 2, then draw a card. (To scry 2, look at the top two cards of your library, then put any number of them o
- oracle_text advisory -- Expressive Iteration: oracle_text diverges (similarity 0.45); scryfall='Look at the top three cards of your library. Put one of them into your hand, put one of them on the bottom of
- oracle_text advisory -- Crackle with Power: oracle_text diverges (similarity 0.17); scryfall='Crackle with Power deals five times X damage to each of up to X targets.'
- oracle_text advisory -- Remand: oracle_text diverges (similarity 0.12); scryfall="Counter target spell. If that spell is countered this way, put it into its owner's hand instead of into that p
- oracle_text advisory -- Memory Lapse: oracle_text diverges (similarity 0.69); scryfall="Counter target spell. If that spell is countered this way, put it on top of its owner's library instead of int
- oracle_text advisory -- Distorting Wake: oracle_text diverges (similarity 0.34); scryfall="Return X target nonland permanents to their owners' hands."
- oracle_text advisory -- Icy Blast: oracle_text diverges (similarity 0.64); scryfall="Tap X target creatures.
Ferocious — If you control a creature with power 4 or greater, those creatures don't
- oracle_text advisory -- Hinata, Dawn-Crowned: oracle_text diverges (similarity 0.31); scryfall='Flying, trample
Spells you cast cost {1} less to cast for each target.
Spells your opponents cast cost {1} m
- oracle_text advisory -- Izzet Boilerworks: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {U}{
- oracle_text advisory -- Orzhov Basilica: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {W}{
- oracle_text advisory -- Soulfire Eruption: oracle_text diverges (similarity 0.28); scryfall="Choose any number of target creatures, planeswalkers, and/or players. For each of them, exile the top card of
- oracle_text advisory -- Magma Opus: oracle_text diverges (similarity 0.46); scryfall='Magma Opus deals 4 damage divided as you choose among any number of targets. Tap two target permanents. Create
- oracle_text advisory -- Reality Spasm: oracle_text diverges (similarity 0.20); scryfall='Choose one —
• Tap X target permanents.
• Untap X target permanents.'
- oracle_text advisory -- Ornithopter of Paradise: oracle_text diverges (similarity 0.13); scryfall='Flying
{T}: Add one mana of any color.'
- oracle_text advisory -- Gamble: oracle_text diverges (similarity 0.22); scryfall='Search your library for a card, put that card into your hand, discard a card at random, then shuffle.'
- oracle_text advisory -- Irencrag Feat: oracle_text diverges (similarity 0.10); scryfall='Add seven {R}. You can cast only one more spell this turn.'
- oracle_text advisory -- Pyretic Ritual: oracle_text diverges (similarity 0.05); scryfall='Add {R}{R}{R}.'
- oracle_text advisory -- Seething Song: oracle_text diverges (similarity 0.08); scryfall='Add {R}{R}{R}{R}{R}.'
- oracle_text advisory -- Desperate Ritual: oracle_text diverges (similarity 0.18); scryfall="Add {R}{R}{R}.
Splice onto Arcane {1}{R} (As you cast an Arcane spell, you may reveal this card from your han
- oracle_text advisory -- Dragonlord Kolaghan: oracle_text diverges (similarity 0.53); scryfall='Flying, haste
Other creatures you control have haste.
Whenever an opponent casts a creature or planeswalker
- oracle_text advisory -- Karrthus, Tyrant of Jund: oracle_text diverges (similarity 0.37); scryfall='Flying, haste
When Karrthus enters, gain control of all Dragons, then untap all Dragons.
Other Dragon creatu
- oracle_text advisory -- Ruby Medallion: oracle_text diverges (similarity 0.17); scryfall='Red spells you cast cost {1} less to cast.'
- oracle_text advisory -- Lotus Bloom: oracle_text diverges (similarity 0.25); scryfall='Suspend 3—{0} (Rather than cast this card from your hand, pay {0} and exile it with three time counters on it.
- oracle_text advisory -- Rite of Flame: oracle_text diverges (similarity 0.21); scryfall='Add {R}{R}, then add {R} for each card named Rite of Flame in each graveyard.'
- oracle_text advisory -- Scourge of Valkas: oracle_text diverges (similarity 0.32); scryfall='Flying
Whenever this creature or another Dragon you control enters, it deals X damage to any target, where X
- oracle_text advisory -- Lathliss, Dragon Queen: oracle_text diverges (similarity 0.31); scryfall='Flying
Whenever another nontoken Dragon you control enters, create a 5/5 red Dragon creature token with flyin
- oracle_text advisory -- Utvara Hellkite: oracle_text diverges (similarity 0.24); scryfall='Flying
Whenever a Dragon you control attacks, create a 6/6 red Dragon creature token with flying.'
- oracle_text advisory -- Dragonstorm: oracle_text diverges (similarity 0.16); scryfall='Search your library for a Dragon permanent card, put it onto the battlefield, then shuffle.
Storm (When you c
- oracle_text advisory -- Apex of Power: oracle_text diverges (similarity 0.15); scryfall='Exile the top seven cards of your library. Until end of turn, you may cast spells from among them.
If this sp
- oracle_text advisory -- Slippery Bogle: oracle_text diverges (similarity 0.41); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Gladecover Scout: oracle_text diverges (similarity 0.76); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Kor Spiritdancer: oracle_text diverges (similarity 0.53); scryfall='This creature gets +2/+2 for each Aura attached to it.
Whenever you cast an Aura spell, you may draw a card.'
- oracle_text advisory -- Light-Paws, Emperor's Voice: oracle_text diverges (similarity 0.74); scryfall='Whenever an Aura you control enters, if you cast it, you may search your library for an Aura card with mana va
- oracle_text advisory -- Ethereal Armor: oracle_text diverges (similarity 0.33); scryfall='Enchant creature
Enchanted creature gets +1/+1 for each enchantment you control and has first strike.'
- oracle_text advisory -- Rancor: oracle_text diverges (similarity 0.68); scryfall="Enchant creature
Enchanted creature gets +2/+0 and has trample.
When this Aura is put into a graveyard from
- oracle_text advisory -- Daybreak Coronet: oracle_text diverges (similarity 0.35); scryfall='Enchant creature with another Aura attached to it
Enchanted creature gets +3/+3 and has first strike, vigilan
- oracle_text advisory -- Armadillo Cloak: oracle_text diverges (similarity 0.77); scryfall='Enchant creature
Enchanted creature gets +2/+2 and has trample.
Whenever enchanted creature deals damage, yo
- oracle_text advisory -- Hyena Umbra: oracle_text diverges (similarity 0.52); scryfall='Enchant creature
Enchanted creature gets +1/+1 and has first strike.
Umbra armor (If enchanted creature woul
- oracle_text advisory -- Spirit Mantle: oracle_text diverges (similarity 0.66); scryfall='Enchant creature
Enchanted creature gets +1/+1 and has protection from creatures.'
- oracle_text advisory -- Spider Umbra: oracle_text diverges (similarity 0.40); scryfall='Enchant creature
Enchanted creature gets +1/+1 and has reach. (It can block creatures with flying.)
Umbra ar
- oracle_text advisory -- Ancestral Mask: oracle_text diverges (similarity 0.59); scryfall='Enchant creature
Enchanted creature gets +2/+2 for each other enchantment on the battlefield.'
- oracle_text advisory -- Alpha Authority: oracle_text diverges (similarity 0.54); scryfall="Enchant creature
Enchanted creature has hexproof and can't be blocked by more than one creature."
- oracle_text advisory -- Gryff's Boon: oracle_text diverges (similarity 0.75); scryfall='Enchant creature
Enchanted creature gets +1/+0 and has flying.
{3}{W}: Return this card from your graveyard
- oracle_text advisory -- Audacity: oracle_text diverges (similarity 0.59); scryfall="Enchant creature
Enchanted creature gets +2/+0 and has trample. (It can deal excess combat damage to the play
- oracle_text advisory -- All That Glitters: oracle_text diverges (similarity 0.57); scryfall='Enchant creature
Enchanted creature gets +1/+1 for each artifact and/or enchantment you control.'
- oracle_text advisory -- Spirit Link: oracle_text diverges (similarity 0.47); scryfall='Enchant creature (Target a creature as you cast this. This card enters attached to that creature.)
Whenever e
- oracle_text advisory -- Lion Umbra: oracle_text diverges (similarity 0.77); scryfall='Enchant modified creature (Equipment, Auras its controller controls, and counters are modifications.)
Enchant
- oracle_text advisory -- Brushland: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.
{T}: Add {G} or {W}. This land deals 1 damage to you.'
- oracle_text advisory -- Branchloft Pathway: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Darkbore Pathway: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {B}.'
- oracle_text advisory -- Goblin King: oracle_text diverges (similarity 0.31); scryfall='Other Goblins get +1/+1 and have mountainwalk.'
- oracle_text advisory -- Goblin Chieftain: oracle_text diverges (similarity 0.41); scryfall='Haste (This creature can attack and {T} as soon as it comes under your control.)
Other Goblin creatures you c
- oracle_text advisory -- Goblin Warchief: oracle_text diverges (similarity 0.52); scryfall='Goblin spells you cast cost {1} less to cast.
Goblins you control have haste.'
- oracle_text advisory -- Goblin Piledriver: oracle_text diverges (similarity 0.43); scryfall="Protection from blue (This creature can't be blocked, targeted, dealt damage, or enchanted by anything blue.)\
- oracle_text advisory -- Goblin Matron: oracle_text diverges (similarity 0.66); scryfall='When this creature enters, you may search your library for a Goblin card, reveal that card, put it into your h
- oracle_text advisory -- Mogg War Marshal: oracle_text diverges (similarity 0.56); scryfall='Echo {1}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Siege-Gang Commander: oracle_text diverges (similarity 0.58); scryfall='When this creature enters, create three 1/1 red Goblin creature tokens.
{1}{R}, Sacrifice a Goblin: This crea
- oracle_text advisory -- Skirk Prospector: oracle_text diverges (similarity 0.31); scryfall='Sacrifice a Goblin: Add {R}.'
- oracle_text advisory -- Krenko, Mob Boss: oracle_text diverges (similarity 0.43); scryfall='{T}: Create X 1/1 red Goblin creature tokens, where X is the number of Goblins you control.'
- oracle_text advisory -- Pashalik Mons: oracle_text diverges (similarity 0.52); scryfall='Whenever Pashalik Mons or another Goblin you control dies, Pashalik Mons deals 1 damage to any target.
{3}{R}
- oracle_text advisory -- Rundvelt Hordemaster: oracle_text diverges (similarity 0.36); scryfall="Other Goblins you control get +1/+1.
Whenever this creature or another Goblin you control dies, exile the top
- oracle_text advisory -- Goblin Lackey: oracle_text diverges (similarity 0.56); scryfall='Whenever this creature deals damage to a player, you may put a Goblin permanent card from your hand onto the b
- oracle_text advisory -- Muxus, Goblin Grandee: oracle_text diverges (similarity 0.46); scryfall='When Muxus enters, reveal the top six cards of your library. Put all Goblin creature cards with mana value 5 o
- oracle_text advisory -- Goblin Chainwhirler: oracle_text diverges (similarity 0.24); scryfall='First strike
When this creature enters, it deals 1 damage to each opponent and each creature and planeswalker
- oracle_text advisory -- Twinshot Sniper: oracle_text diverges (similarity 0.50); scryfall='Reach
When this creature enters, it deals 2 damage to any target.
Channel — {1}{R}, Discard this card: It de
- oracle_text advisory -- Stingscourger: oracle_text diverges (similarity 0.71); scryfall="Echo {3}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Three Tree City: oracle_text diverges (similarity 0.47); scryfall='As Three Tree City enters, choose a creature type.
{T}: Add {C}.
{2}, {T}: Choose a color. Add an amount of
- oracle_text advisory -- Hunted Phantasm: oracle_text diverges (similarity 0.34); scryfall="This creature can't be blocked.
When this creature enters, target opponent creates five 1/1 red Goblin creatu
- oracle_text advisory -- Suture Priest: oracle_text diverges (similarity 0.49); scryfall='Whenever another creature you control enters, you may gain 1 life.
Whenever a creature an opponent controls e
- oracle_text advisory -- Massacre Wurm: oracle_text diverges (similarity 0.38); scryfall='When this creature enters, creatures your opponents control get -2/-2 until end of turn.
Whenever a creature
- oracle_text advisory -- Soul Warden: oracle_text diverges (similarity 0.15); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- Essence Warden: oracle_text diverges (similarity 0.24); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- City of Brass: oracle_text diverges (similarity 0.19); scryfall='Whenever this land becomes tapped, it deals 1 damage to you.
{T}: Add one mana of any color.'
- oracle_text advisory -- Defense of the Heart: oracle_text diverges (similarity 0.43); scryfall='At the beginning of your upkeep, if an opponent controls three or more creatures, sacrifice this enchantment,
- oracle_text advisory -- Sylvan Scrying: oracle_text diverges (similarity 0.47); scryfall='Search your library for a land card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Crop Rotation: oracle_text diverges (similarity 0.42); scryfall='As an additional cost to cast this spell, sacrifice a land.
Search your library for a land card, put that car
- oracle_text advisory -- Varchild's War-Riders: oracle_text diverges (similarity 0.58); scryfall='Cumulative upkeep—Have an opponent create a 1/1 red Survivor creature token. (At the beginning of your upkeep,
- oracle_text advisory -- Azorius Chancery: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {W}{
- oracle_text advisory -- Tree of Tales: oracle_text diverges (similarity 0.15); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Misty Rainforest: oracle_text diverges (similarity 0.29); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Island card, put it onto the battlef
- oracle_text advisory -- Verdant Catacombs: oracle_text diverges (similarity 0.26); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Forest card, put it onto the battlefi
- oracle_text advisory -- Scalding Tarn: oracle_text diverges (similarity 0.17); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for an Island or Mountain card, put it onto the batt
- oracle_text advisory -- Cosmic Spider-Man: oracle_text diverges (similarity 0.33); scryfall='Flying, first strike, trample, lifelink, haste
At the beginning of combat on your turn, other Spiders you con
- oracle_text advisory -- Mana Cannons: oracle_text diverges (similarity 0.44); scryfall='Whenever you cast a multicolored spell, this enchantment deals X damage to any target, where X is the number o
- oracle_text advisory -- Ancient Cornucopia: oracle_text diverges (similarity 0.43); scryfall="Whenever you cast a spell that's one or more colors, you may gain 1 life for each of that spell's colors. Do t
- oracle_text advisory -- Two-Headed Hellkite: oracle_text diverges (similarity 0.26); scryfall='Flying, menace, haste
Whenever this creature attacks, draw two cards.'
- oracle_text advisory -- Progenitus: oracle_text diverges (similarity 0.28); scryfall="Protection from everything
If Progenitus would be put into a graveyard from anywhere, reveal Progenitus and s
- oracle_text advisory -- Faeburrow Elder: oracle_text diverges (similarity 0.36); scryfall='Vigilance
This creature gets +1/+1 for each color among permanents you control.
{T}: For each color among pe
- oracle_text advisory -- Bloom Tender: oracle_text diverges (similarity 0.52); scryfall='Vivid — {T}: For each color among permanents you control, add one mana of that color.'
- oracle_text advisory -- Deathrite Shaman: oracle_text diverges (similarity 0.46); scryfall='{T}: Exile target land card from a graveyard. Add one mana of any color. (Activate only as an instant.)
{B},
- oracle_text advisory -- Lightning Greaves: oracle_text diverges (similarity 0.18); scryfall="Equipped creature has haste and shroud. (It can't be the target of spells or abilities.)
Equip {0}"
- oracle_text advisory -- Maelstrom Archangel: oracle_text diverges (similarity 0.31); scryfall='Flying
Whenever this creature deals combat damage to a player, you may cast a spell from your hand without pa
- oracle_text advisory -- Jared Carthalion: oracle_text diverges (similarity 0.60); scryfall="+1: Create a 3/3 Kavu creature token with trample that's all colors.
−3: Choose up to two target creatures. F
- oracle_text advisory -- Nicol Bolas, Planeswalker: oracle_text diverges (similarity 0.21); scryfall="+3: Destroy target noncreature permanent.
−2: Gain control of target creature.
−9: Nicol Bolas deals 7 damag
- oracle_text advisory -- Oko, Thief of Crowns: oracle_text diverges (similarity 0.42); scryfall='+2: Create a Food token. (It\'s an artifact with "{2}, {T}, Sacrifice this token: You gain 3 life.")
+1: Targ
- oracle_text advisory -- Garth One-Eye: oracle_text diverges (similarity 0.39); scryfall="{T}: Choose a card name that hasn't been chosen from among Disenchant, Braingeyser, Terror, Shivan Dragon, Reg
- oracle_text advisory -- Black Lotus: oracle_text diverges (similarity 0.36); scryfall='{T}, Sacrifice this artifact: Add three mana of any one color.'
- oracle_text advisory -- Braingeyser: oracle_text diverges (similarity 0.08); scryfall='Target player draws X cards.'
- oracle_text advisory -- Terror: oracle_text diverges (similarity 0.32); scryfall="Destroy target nonartifact, nonblack creature. It can't be regenerated."
- oracle_text advisory -- Shivan Dragon: oracle_text diverges (similarity 0.32); scryfall='Flying
{R}: This creature gets +1/+0 until end of turn.'
- oracle_text advisory -- Regrowth: oracle_text diverges (similarity 0.46); scryfall='Return target card from your graveyard to your hand.'
- oracle_text advisory -- Unite the Coalition: oracle_text diverges (similarity 0.35); scryfall="Choose five. You may choose the same mode more than once.
• Target permanent phases out.
• Target player dra
- oracle_text advisory -- Disenchant: oracle_text diverges (similarity 0.21); scryfall='Destroy target artifact or enchantment.'
- oracle_text advisory -- Mirrorwing Dragon: oracle_text diverges (similarity 0.42); scryfall='Flying
Whenever a player casts an instant or sorcery spell that targets only this creature, that player copie
- oracle_text advisory -- Zada, Hedron Grinder: oracle_text diverges (similarity 0.57); scryfall='Whenever you cast an instant or sorcery spell that targets only Zada, copy that spell for each other creature
- oracle_text advisory -- Goblin Instigator: oracle_text diverges (similarity 0.32); scryfall='When this creature enters, create a 1/1 red Goblin creature token.'
- oracle_text advisory -- Fists of Flame: oracle_text diverges (similarity 0.36); scryfall="Draw a card. Until end of turn, target creature gains trample and gets +1/+0 for each card you've drawn this t
- oracle_text advisory -- Luxurious Libation: oracle_text diverges (similarity 0.24); scryfall='Target creature gets +X/+X until end of turn. Create a 1/1 green and white Citizen creature token.'
- oracle_text advisory -- Fortifying Draught: oracle_text diverges (similarity 0.33); scryfall='You gain 2 life. Target creature gets +X/+X until end of turn, where X is the amount of life you gained this t
- oracle_text advisory -- Gold Rush: oracle_text diverges (similarity 0.36); scryfall='Create a Treasure token. Until end of turn, up to one target creature gets +2/+2 for each Treasure you control
- oracle_text advisory -- Ancestral Anger: oracle_text diverges (similarity 0.52); scryfall='Target creature gains trample and gets +X/+0 until end of turn, where X is 1 plus the number of cards named An
- oracle_text advisory -- Oracle's Restoration: oracle_text diverges (similarity 0.10); scryfall='Target creature you control gets +1/+1 until end of turn. You draw a card and gain 1 life.'
- oracle_text advisory -- Expedite: oracle_text diverges (similarity 0.29); scryfall='Target creature gains haste until end of turn.
Draw a card.'
- oracle_text advisory -- Impolite Entrance: oracle_text diverges (similarity 0.18); scryfall='Target creature gains trample and haste until end of turn.
Draw a card.'
- oracle_text advisory -- Scale the Heights: oracle_text diverges (similarity 0.49); scryfall='Put a +1/+1 counter on up to one target creature. You gain 2 life. You may play an additional land this turn.\
- oracle_text advisory -- Twinflame: oracle_text diverges (similarity 0.42); scryfall="Strive — This spell costs {2}{R} more to cast for each target beyond the first.
Choose any number of target c
- oracle_text advisory -- Gruul Turf: oracle_text diverges (similarity 0.39); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {R}{
- oracle_text advisory -- Kazandu Refuge: oracle_text diverges (similarity 0.50); scryfall='This land enters tapped.
When this land enters, you gain 1 life.
{T}: Add {R} or {G}.'
- oracle_text advisory -- Rootbound Crag: oracle_text diverges (similarity 0.43); scryfall='This land enters tapped unless you control a Mountain or a Forest.
{T}: Add {R} or {G}.'
- oracle_text advisory -- Colossus Hammer: oracle_text diverges (similarity 0.25); scryfall='Equipped creature gets +10/+10 and loses flying.
Equip {8} ({8}: Attach to target creature you control. Equip
- oracle_text advisory -- Loxodon Warhammer: oracle_text diverges (similarity 0.36); scryfall='Equipped creature gets +3/+0 and has trample and lifelink.
Equip {3}'
- oracle_text advisory -- Shadowspear: oracle_text diverges (similarity 0.54); scryfall='Equipped creature gets +1/+1 and has trample and lifelink.
{1}: Permanents your opponents control lose hexpro
- oracle_text advisory -- Grafted Wargear: oracle_text diverges (similarity 0.52); scryfall='Equipped creature gets +3/+2.
Whenever this Equipment becomes unattached from a permanent, sacrifice that per
- oracle_text advisory -- O-Naginata: oracle_text diverges (similarity 0.49); scryfall='This Equipment can be attached only to a creature with power 3 or greater.
Equipped creature gets +3/+0 and h
- oracle_text advisory -- Ancient Den: oracle_text diverges (similarity 0.02); scryfall='{T}: Add {W}.'
- oracle_text advisory -- Bone Saw: oracle_text diverges (similarity 0.29); scryfall='Equipped creature gets +1/+0.
Equip {1} ({1}: Attach to target creature you control. Equip only as a sorcery.
- oracle_text advisory -- Paradise Mantle: oracle_text diverges (similarity 0.10); scryfall='Equipped creature has "{T}: Add one mana of any color."
Equip {1}'
- oracle_text advisory -- Cathar's Shield: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +0/+3 and has vigilance.
Equip {3} ({3}: Attach to target creature you control. Equip
- oracle_text advisory -- Accorder's Shield: oracle_text diverges (similarity 0.23); scryfall="Equipped creature gets +0/+3 and has vigilance. (Attacking doesn't cause it to tap.)
Equip {3} ({3}: Attach t
- oracle_text advisory -- Kite Shield: oracle_text diverges (similarity 0.52); scryfall='Equipped creature gets +0/+3.
Equip {3} ({3}: Attach to target creature you control. Equip only as a sorcery.
- oracle_text advisory -- Spidersilk Net: oracle_text diverges (similarity 0.22); scryfall='Equipped creature gets +0/+2 and has reach. (It can block creatures with flying.)
Equip {2} ({2}: Attach to t
- oracle_text advisory -- Golem-Skin Gauntlets: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +1/+0 for each Equipment attached to it.
Equip {2} ({2}: Attach to target creature you
- oracle_text advisory -- Skateboard: oracle_text diverges (similarity 0.19); scryfall='When this Equipment enters, tap target permanent.
Equipped creature gets +1/+0 and has haste.
Equip {1} ({1}
- oracle_text advisory -- Dragonfire Blade: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +2/+2 and has hexproof from monocolored.
Equip {4}. This ability costs {1} less to act
- oracle_text advisory -- Deconstruction Hammer: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +1/+1 and has "{3}, {T}, Sacrifice Deconstruction Hammer: Destroy target artifact or en
- oracle_text advisory -- Sram, Senior Edificer: oracle_text diverges (similarity 0.08); scryfall='Whenever you cast an Aura, Equipment, or Vehicle spell, draw a card.'
- oracle_text advisory -- Cid, Freeflier Pilot: oracle_text diverges (similarity 0.13); scryfall='Equipment and Vehicle spells you cast cost {1} less to cast.
Jump — During your turn, Cid has flying.
{2}, {
- oracle_text advisory -- Sigarda's Aid: oracle_text diverges (similarity 0.14); scryfall='You may cast Aura and Equipment spells as though they had flash.
Whenever an Equipment you control enters, yo
- oracle_text advisory -- Dwalin, Weaponmaster: oracle_text diverges (similarity 0.12); scryfall='First strike
Whenever Dwalin enters or attacks, put a hone counter on each Equipment you control. (Each hone
- oracle_text advisory -- Ardenn, Intrepid Archaeologist: oracle_text diverges (similarity 0.17); scryfall='At the beginning of combat on your turn, you may attach any number of Auras and Equipment you control to targe
- oracle_text advisory -- Umezawa's Jitte: oracle_text diverges (similarity 0.47); scryfall="Whenever equipped creature deals combat damage, put two charge counters on Umezawa's Jitte.
Remove a charge c
- oracle_text advisory -- Kor Duelist: oracle_text diverges (similarity 0.43); scryfall='As long as this creature is equipped, it has double strike. (It deals both first-strike and regular combat dam
- oracle_text advisory -- Puresteel Paladin: oracle_text diverges (similarity 0.34); scryfall='Whenever an Equipment you control enters, you may draw a card.
Metalcraft — Equipment you control have equip
- oracle_text advisory -- Balan, Wandering Knight: oracle_text diverges (similarity 0.30); scryfall='First strike
Balan has double strike as long as two or more Equipment are attached to it.
{1}{W}: Attach all
- oracle_text advisory -- Armored Skyhunter: oracle_text diverges (similarity 0.49); scryfall='Flying
Whenever this creature attacks, look at the top six cards of your library. You may put an Aura or Equi
- oracle_text advisory -- Kemba, Kha Regent: oracle_text diverges (similarity 0.34); scryfall='At the beginning of your upkeep, create a 2/2 white Cat creature token for each Equipment attached to Kemba.'
- oracle_text advisory -- Stoneforge Mystic: oracle_text diverges (similarity 0.44); scryfall='When this creature enters, you may search your library for an Equipment card, reveal it, put it into your hand
- oracle_text advisory -- Unexpectedly Absent: oracle_text diverges (similarity 0.28); scryfall="Put target nonland permanent into its owner's library just beneath the top X cards of that library."
- oracle_text advisory -- Boros Garrison: oracle_text diverges (similarity 0.34); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {R}{
- oracle_text advisory -- Elvish Archdruid: oracle_text diverges (similarity 0.38); scryfall='Other Elf creatures you control get +1/+1.
{T}: Add {G} for each Elf you control.'
- oracle_text advisory -- Priest of Titania: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {G} for each Elf on the battlefield.'
- oracle_text advisory -- Arbor Elf: oracle_text diverges (similarity 0.03); scryfall='{T}: Untap target Forest.'
- oracle_text advisory -- Wirewood Lodge: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.
{G}, {T}: Untap target Elf.'
- oracle_text advisory -- Worldly Tutor: oracle_text diverges (similarity 0.41); scryfall='Search your library for a creature card, reveal it, then shuffle and put the card on top.'
- oracle_text advisory -- Mirri's Guile: oracle_text diverges (similarity 0.44); scryfall='At the beginning of your upkeep, you may look at the top three cards of your library, then put them back in an
- oracle_text advisory -- Call of the Wild: oracle_text diverges (similarity 0.52); scryfall="{2}{G}{G}: Reveal the top card of your library. If it's a creature card, put it onto the battlefield. Otherwis
- oracle_text advisory -- Hornet Queen: oracle_text diverges (similarity 0.41); scryfall='Flying, deathtouch
When this creature enters, create four 1/1 green Insect creature tokens with flying and de
- oracle_text advisory -- Terastodon: oracle_text diverges (similarity 0.21); scryfall='When this creature enters, you may destroy up to three target noncreature permanents. For each permanent put i
- oracle_text advisory -- Elderscale Wurm: oracle_text diverges (similarity 0.53); scryfall='Trample
When this creature enters, if your life total is less than 7, your life total becomes 7.
As long as
- oracle_text advisory -- Craterhoof Behemoth: oracle_text diverges (similarity 0.44); scryfall='Haste
When this creature enters, creatures you control gain trample and get +X/+X until end of turn, where X
- oracle_text advisory -- Worldspine Wurm: oracle_text diverges (similarity 0.39); scryfall="Trample
When this creature dies, create three 5/5 green Wurm creature tokens with trample.
When Worldspine W
- oracle_text advisory -- Vaultborn Tyrant: oracle_text diverges (similarity 0.47); scryfall="Trample
Whenever this creature or another creature you control with power 4 or greater enters, you gain 3 lif
- oracle_text advisory -- Natural Order: oracle_text diverges (similarity 0.38); scryfall='As an additional cost to cast this spell, sacrifice a green creature.
Search your library for a green creatur
- oracle_text advisory -- Turntimber Symbiosis: oracle_text diverges (similarity 0.45); scryfall='Look at the top seven cards of your library. You may put a creature card from among them onto the battlefield.
- oracle_text advisory -- Boros Reckoner: oracle_text diverges (similarity 0.17); scryfall='Whenever this creature is dealt damage, it deals that much damage to any target.
{R/W}: This creature gains f
- oracle_text advisory -- Burning-Fist Minotaur: oracle_text diverges (similarity 0.12); scryfall='First strike
{1}{R}, Discard a card: This creature gets +2/+0 until end of turn.'
- oracle_text advisory -- Deathbellow Raider: oracle_text diverges (similarity 0.16); scryfall='This creature attacks each combat if able.
{2}{B}: Regenerate this creature.'
- oracle_text advisory -- Fanatic of Mogis: oracle_text diverges (similarity 0.39); scryfall='When this creature enters, it deals damage to each opponent equal to your devotion to red. (Each {R} in the ma
- oracle_text advisory -- Gnarled Scarhide: oracle_text diverges (similarity 0.34); scryfall="Bestow {3}{B} (If you cast this card for its bestow cost, it's an Aura spell with enchant creature. It becomes
- oracle_text advisory -- Kragma Warcaller: oracle_text diverges (similarity 0.33); scryfall='Minotaur creatures you control have haste.
Whenever a Minotaur you control attacks, it gets +2/+0 until end o
- oracle_text advisory -- Neheb, the Worthy: oracle_text diverges (similarity 0.25); scryfall='First strike
Other Minotaurs you control have first strike.
As long as you have one or fewer cards in hand,
- oracle_text advisory -- Rageblood Shaman: oracle_text diverges (similarity 0.35); scryfall='Trample
Other Minotaur creatures you control get +1/+1 and have trample.'
- oracle_text advisory -- Ragemonger: oracle_text diverges (similarity 0.45); scryfall='Minotaur spells you cast cost {B}{R} less to cast. This effect reduces only the amount of colored mana you pay
- oracle_text advisory -- Rakdos Carnarium: oracle_text diverges (similarity 0.36); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {B}{
- oracle_text advisory -- Sethron, Hurloon General: oracle_text diverges (similarity 0.34); scryfall='Whenever Sethron or another nontoken Minotaur you control enters, create a 2/3 red Minotaur creature token.
{
- oracle_text advisory -- Slaughter-Priest of Mogis: oracle_text diverges (similarity 0.21); scryfall='Whenever you sacrifice a permanent, this creature gets +2/+0 until end of turn.
{2}, Sacrifice another creatu
- oracle_text advisory -- Atsushi, the Blazing Sky: oracle_text diverges (similarity 0.43); scryfall='Flying, trample
When Atsushi dies, choose one —
• Exile the top two cards of your library. Until the end of
- oracle_text advisory -- Inferno of the Star Mounts: oracle_text diverges (similarity 0.35); scryfall="This spell can't be countered.
Flying, haste
{R}: Inferno of the Star Mounts gets +1/+0 until end of turn. W
- oracle_text advisory -- Dragon Tempest: oracle_text diverges (similarity 0.34); scryfall='Whenever a creature you control with flying enters, it gains haste until end of turn.
Whenever a Dragon you c
- oracle_text advisory -- Urza's Incubator: oracle_text diverges (similarity 0.15); scryfall='As this artifact enters, choose a creature type.
Creature spells of the chosen type cost {2} less to cast.'
- oracle_text advisory -- Mind Stone: oracle_text diverges (similarity 0.25); scryfall='{T}: Add {C}.
{1}, {T}, Sacrifice this artifact: Draw a card.'
- oracle_text advisory -- Fire Diamond: oracle_text diverges (similarity 0.11); scryfall='This artifact enters tapped.
{T}: Add {R}.'
- oracle_text advisory -- Dragonspeaker Shaman: oracle_text diverges (similarity 0.15); scryfall='Dragon spells you cast cost {2} less to cast.'
- oracle_text advisory -- Glorybringer: oracle_text diverges (similarity 0.46); scryfall="Flying, haste
You may exert this creature as it attacks. When you do, it deals 4 damage to target non-Dragon
- oracle_text advisory -- Haven of the Spirit Dragon: oracle_text diverges (similarity 0.32); scryfall='{T}: Add {C}.
{T}: Add one mana of any color. Spend this mana only to cast a Dragon creature spell.
{2}, {T}
- oracle_text advisory -- Nest Invader: oracle_text diverges (similarity 0.27); scryfall='When this creature enters, create a 0/1 colorless Eldrazi Spawn creature token. It has "Sacrifice this token:
- oracle_text advisory -- Young Pyromancer: oracle_text diverges (similarity 0.28); scryfall='Whenever you cast an instant or sorcery spell, create a 1/1 red Elemental creature token.'
- oracle_text advisory -- Undercellar Myconid: oracle_text diverges (similarity 0.39); scryfall='Whenever this creature enters or dies, create a 1/1 green Saproling creature token.
{T}: Add one mana of any
- oracle_text advisory -- Frontline Heroism: oracle_text diverges (similarity 0.39); scryfall='When this enchantment enters, create a 1/1 red Soldier creature token with haste.
Whenever you cast a spell t
- oracle_text advisory -- Adarkar Wastes: oracle_text diverges (similarity 0.29); scryfall='{T}: Add {C}.
{T}: Add {W} or {U}. This land deals 1 damage to you.'
- oracle_text advisory -- Caves of Koilos: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.
{T}: Add {W} or {B}. This land deals 1 damage to you.'
- oracle_text advisory -- Yavimaya Coast: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.
{T}: Add {G} or {U}. This land deals 1 damage to you.'
- oracle_text advisory -- Llanowar Wastes: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.
{T}: Add {B} or {G}. This land deals 1 damage to you.'
- oracle_text advisory -- Conservatory: oracle_text diverges (similarity 0.64); scryfall='This land enters tapped.
{T}: Add {G} or {W}.
{4}, {T}: Investigate. (Create a Clue token. It\'s an artifact
- oracle_text advisory -- Shivan Gorge: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.
{2}{R}, {T}: Shivan Gorge deals 1 damage to each opponent.'
- oracle_text advisory -- Mariposa Military Base: oracle_text diverges (similarity 0.26); scryfall='You may have this land enter tapped. If you do, you get two rad counters.
{T}: Add {C}.
{5}, {T}: Draw a car
- oracle_text advisory -- Eldrazi Displacer: oracle_text diverges (similarity 0.47); scryfall="Devoid (This card has no color.)
{2}{C}: Exile another target creature, then return it to the battlefield tap
- oracle_text advisory -- Emiel the Blessed: oracle_text diverges (similarity 0.48); scryfall="{3}: Exile another target creature you control, then return it to the battlefield under its owner's control.

- oracle_text advisory -- Cloud of Faeries: oracle_text diverges (similarity 0.25); scryfall='Flying
When this creature enters, untap up to two lands.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Peregrine Drake: oracle_text diverges (similarity 0.22); scryfall='Flying
When this creature enters, untap up to five lands.'
- oracle_text advisory -- Wild Growth: oracle_text diverges (similarity 0.50); scryfall='Enchant land
Whenever enchanted land is tapped for mana, its controller adds an additional {G}.'
- oracle_text advisory -- Cryptolith Rite: oracle_text diverges (similarity 0.05); scryfall='Creatures you control have "{T}: Add one mana of any color."'
- oracle_text advisory -- Overgrowth: oracle_text diverges (similarity 0.61); scryfall='Enchant land
Whenever enchanted land is tapped for mana, its controller adds an additional {G}{G}.'
- oracle_text advisory -- Fertile Ground: oracle_text diverges (similarity 0.49); scryfall='Enchant land
Whenever enchanted land is tapped for mana, its controller adds an additional one mana of any co
- oracle_text advisory -- Trace of Abundance: oracle_text diverges (similarity 0.34); scryfall="Enchant land
Enchanted land has shroud. (It can't be the target of spells or abilities.)
Whenever enchanted
- oracle_text advisory -- Training Grounds: oracle_text diverges (similarity 0.49); scryfall="Activated abilities of creatures you control cost {2} less to activate. This effect can't reduce the mana in t
- oracle_text advisory -- Eladamri's Call: oracle_text diverges (similarity 0.61); scryfall='Search your library for a creature card, reveal that card, put it into your hand, then shuffle.'
- oracle_text advisory -- Stroke of Genius: oracle_text diverges (similarity 0.04); scryfall='Target player draws X cards.'
- oracle_text advisory -- Vexing Shusher: oracle_text diverges (similarity 0.05); scryfall="This spell can't be countered.
{R/G}: Target spell can't be countered."
- oracle_text advisory -- Essence Depleter: oracle_text diverges (similarity 0.13); scryfall='Devoid (This card has no color.)
{1}{C}: Target opponent loses 1 life and you gain 1 life. ({C} represents co
- oracle_text advisory -- Dimensional Infiltrator: oracle_text diverges (similarity 0.14); scryfall="Devoid (This card has no color.)
Flash
Flying
{1}{C}: Target opponent exiles the top card of their library.
- oracle_text advisory -- Living Wish: oracle_text diverges (similarity 0.08); scryfall='You may reveal a creature or land card you own from outside the game and put it into your hand. Exile Living W
- oracle_text advisory -- Aether Hub: oracle_text diverges (similarity 0.08); scryfall='When this land enters, you get {E} (an energy counter).
{T}: Add {C}.
{T}, Pay {E}: Add one mana of any colo
- oracle_text advisory -- Maelstrom Wanderer: oracle_text diverges (similarity 0.34); scryfall='Creatures you control have haste.
Cascade, cascade (When you cast this spell, exile cards from the top of you
- oracle_text advisory -- Annoyed Altisaur: oracle_text diverges (similarity 0.50); scryfall='Reach, trample
Cascade (When you cast this spell, exile cards from the top of your library until you exile a
- oracle_text advisory -- Sakashima's Protege: oracle_text diverges (similarity 0.28); scryfall='Flash
Cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland c
- oracle_text advisory -- Boarding Party: oracle_text diverges (similarity 0.62); scryfall='Haste
Cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland c
- oracle_text advisory -- Breaching Dragonstorm: oracle_text diverges (similarity 0.26); scryfall="When this enchantment enters, exile cards from the top of your library until you exile a nonland card. You may
- oracle_text advisory -- Call Forth the Tempest: oracle_text diverges (similarity 0.40); scryfall="Cascade, cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland
- oracle_text advisory -- Creative Technique: oracle_text diverges (similarity 0.33); scryfall='Demonstrate (When you cast this spell, you may copy it. If you do, choose an opponent to also copy it.)
Shuff
- oracle_text advisory -- Dwarven Ruins: oracle_text diverges (similarity 0.09); scryfall='This land enters tapped.
{T}: Add {R}.
{T}, Sacrifice this land: Add {R}{R}.'
- oracle_text advisory -- Svyelunite Temple: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.
{T}: Add {U}.
{T}, Sacrifice this land: Add {U}{U}.'
- oracle_text advisory -- Hickory Woodlot: oracle_text diverges (similarity 0.24); scryfall='This land enters tapped with two depletion counters on it.
{T}, Remove a depletion counter from this land: Ad
- oracle_text advisory -- Melira, Sylvok Outcast: oracle_text diverges (similarity 0.34); scryfall="You can't get poison counters.
Creatures you control can't have -1/-1 counters put on them.
Creatures your o
- oracle_text advisory -- Vizier of Remedies: oracle_text diverges (similarity 0.55); scryfall='If one or more -1/-1 counters would be put on a creature you control, that many -1/-1 counters minus one are p
- oracle_text advisory -- Kitchen Finks: oracle_text diverges (similarity 0.44); scryfall="When this creature enters, you gain 2 life.
Persist (When this creature dies, if it had no -1/-1 counters on
- oracle_text advisory -- Murderous Redcap: oracle_text diverges (similarity 0.51); scryfall="When this creature enters, it deals damage equal to its power to any target.
Persist (When this creature dies
- oracle_text advisory -- Carrion Feeder: oracle_text diverges (similarity 0.28); scryfall="This creature can't block.
Sacrifice a creature: Put a +1/+1 counter on this creature."
- oracle_text advisory -- Bloodthrone Vampire: oracle_text diverges (similarity 0.26); scryfall='Sacrifice a creature: This creature gets +2/+2 until end of turn.'
- oracle_text advisory -- Recruiter of the Guard: oracle_text diverges (similarity 0.35); scryfall='When this creature enters, you may search your library for a creature card with toughness 2 or less, reveal it
- oracle_text advisory -- Ranger of Eos: oracle_text diverges (similarity 0.33); scryfall='When this creature enters, you may search your library for up to two creature cards with mana value 1 or less,
- oracle_text advisory -- Severance Priest: oracle_text diverges (similarity 0.40); scryfall="Deathtouch
When this creature enters, target opponent reveals their hand. You may choose a nonland card from
- oracle_text advisory -- Birthing Pod: oracle_text diverges (similarity 0.25); scryfall="({G/P} can be paid with either {G} or 2 life.)
{1}{G/P}, {T}, Sacrifice a creature: Search your library for a
- oracle_text advisory -- Chord of Calling: oracle_text diverges (similarity 0.25); scryfall="Convoke (Your creatures can help cast this spell. Each creature you tap while casting this spell pays for {1}
- oracle_text advisory -- Reveillark: oracle_text diverges (similarity 0.22); scryfall="Flying
When this creature leaves the battlefield, return up to two target creature cards with power 2 or less
- oracle_text advisory -- Felidar Guardian: oracle_text diverges (similarity 0.18); scryfall="When this creature enters, you may exile another target permanent you control, then return that card to the ba
- oracle_text advisory -- Voice of Resurgence: oracle_text diverges (similarity 0.32); scryfall='Whenever an opponent casts a spell during your turn and when this creature dies, create a green and white Elem
- oracle_text advisory -- Scavenging Ooze: oracle_text diverges (similarity 0.23); scryfall='{G}: Exile target card from a graveyard. If it was a creature card, put a +1/+1 counter on this creature and y
- oracle_text advisory -- Ravenous Chupacabra: oracle_text diverges (similarity 0.18); scryfall='When this creature enters, destroy target creature an opponent controls.'
- oracle_text advisory -- Reclamation Sage: oracle_text diverges (similarity 0.16); scryfall='When this creature enters, you may destroy target artifact or enchantment.'
- oracle_text advisory -- Celes, Rune Knight: oracle_text diverges (similarity 0.25); scryfall='When Celes enters, discard any number of cards, then draw that many cards plus one.
Whenever one or more othe
- oracle_text advisory -- Drifting Meadow: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.
{T}: Add {W}.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Polluted Mire: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.
{T}: Add {B}.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Smoldering Crater: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.
{T}: Add {R}.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Blasted Landscape: oracle_text diverges (similarity 0.30); scryfall='{T}: Add {C}.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Fetid Pools: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {U} or {B}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Irrigated Farmland: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {W} or {U}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Canyon Slough: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {B} or {R}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Sheltered Thicket: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {R} or {G}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Scattered Groves: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {G} or {W}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Glittering Massif: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {R} or {W}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Festering Thicket: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {B} or {G}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Capital City: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.
{1}, {T}: Add one mana of any color.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Forsake the Worldly: oracle_text diverges (similarity 0.22); scryfall='Exile target artifact or enchantment.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Fluctuator: oracle_text diverges (similarity 0.17); scryfall='Cycling abilities you activate cost {2} less to activate.'
- oracle_text advisory -- Drannith Stinger: oracle_text diverges (similarity 0.26); scryfall='Whenever you cycle another card, this creature deals 1 damage to each opponent.
Cycling {1} ({1}, Discard thi
- oracle_text advisory -- Hollow One: oracle_text diverges (similarity 0.20); scryfall="This spell costs {2} less to cast for each card you've cycled or discarded this turn.
Cycling {2} ({2}, Disca
- oracle_text advisory -- Unearth: oracle_text diverges (similarity 0.24); scryfall='Return target creature card with mana value 3 or less from your graveyard to the battlefield.
Cycling {2} ({2
- oracle_text advisory -- Scrying Sheets: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.
{1}{S}, {T}: Look at the top card of your library. If that card is snow, you may reveal it and
- oracle_text advisory -- Skred: oracle_text diverges (similarity 0.23); scryfall='Skred deals damage to target creature equal to the number of snow permanents you control.'
- oracle_text advisory -- Coldsteel Heart: oracle_text diverges (similarity 0.11); scryfall='This artifact enters tapped.
As this artifact enters, choose a color.
{T}: Add one mana of the chosen color.
- oracle_text advisory -- Abominable Treefolk: oracle_text diverges (similarity 0.35); scryfall="Trample
Abominable Treefolk's power and toughness are each equal to the number of snow permanents you control
- oracle_text advisory -- Arcum's Astrolabe: oracle_text diverges (similarity 0.25); scryfall='({S} can be paid with one mana from a snow source.)
When this artifact enters, draw a card.
{1}, {T}: Add on
- oracle_text advisory -- Frost Augur: oracle_text diverges (similarity 0.39); scryfall="{S}, {T}: Look at the top card of your library. If it's a snow card, you may reveal it and put it into your ha
- oracle_text advisory -- Ice-Fang Coatl: oracle_text diverges (similarity 0.21); scryfall='Flash
Flying
When this creature enters, draw a card.
This creature has deathtouch as long as you control at
- oracle_text advisory -- Jorn, God of Winter: oracle_text diverges (similarity 0.10); scryfall='Whenever Jorn attacks, untap each snow permanent you control.'
- oracle_text advisory -- Kaldring, the Rimestaff: oracle_text diverges (similarity 0.17); scryfall='{T}: You may play target snow permanent card from your graveyard this turn. If you do, it enters tapped.'
- oracle_text advisory -- Marit Lage's Slumber: oracle_text diverges (similarity 0.35); scryfall="Whenever Marit Lage's Slumber or another snow permanent you control enters, scry 1.
At the beginning of your
- oracle_text advisory -- Rimefeather Owl: oracle_text diverges (similarity 0.26); scryfall="Flying
Rimefeather Owl's power and toughness are each equal to the number of snow permanents on the battlefie
- oracle_text advisory -- Rimescale Dragon: oracle_text diverges (similarity 0.29); scryfall="Flying
{2}{S}: Tap target creature and put an ice counter on it. ({S} can be paid with one mana from a snow s
- oracle_text advisory -- Soul's Attendant: oracle_text diverges (similarity 0.12); scryfall='Whenever another creature enters, you may gain 1 life.'
- oracle_text advisory -- Auriok Champion: oracle_text diverges (similarity 0.11); scryfall='Protection from black and from red
Whenever another creature enters, you may gain 1 life.'
- oracle_text advisory -- Ocelot Pride: oracle_text diverges (similarity 0.12); scryfall="First strike, lifelink
Ascend (If you control ten or more permanents, you get the city's blessing for the res
- oracle_text advisory -- Serra Ascendant: oracle_text diverges (similarity 0.21); scryfall='Lifelink (Damage dealt by this creature also causes you to gain that much life.)
As long as you have 30 or mo
- oracle_text advisory -- Ajani's Pridemate: oracle_text diverges (similarity 0.09); scryfall='Whenever you gain life, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Voice of the Blessed: oracle_text diverges (similarity 0.26); scryfall='Whenever you gain life, put a +1/+1 counter on this creature.
As long as this creature has four or more +1/+1
- oracle_text advisory -- Daxos, Blessed by the Sun: oracle_text diverges (similarity 0.18); scryfall="Daxos's toughness is equal to your devotion to white. (Each {W} in the mana costs of permanents you control co
- oracle_text advisory -- Archangel of Thune: oracle_text diverges (similarity 0.24); scryfall='Flying
Lifelink (Damage dealt by this creature also causes you to gain that much life.)
Whenever you gain li
- oracle_text advisory -- Heliod, Sun-Crowned: oracle_text diverges (similarity 0.13); scryfall="Indestructible
As long as your devotion to white is less than five, Heliod isn't a creature.
Whenever you ga
- oracle_text advisory -- Ranger-Captain of Eos: oracle_text diverges (similarity 0.12); scryfall="When this creature enters, you may search your library for a creature card with mana value 1 or less, reveal i
- oracle_text advisory -- Ajani, Strength of the Pride: oracle_text diverges (similarity 0.27); scryfall='+1: You gain life equal to the number of creatures you control plus the number of planeswalkers you control.

- oracle_text advisory -- Apex Altisaur: oracle_text diverges (similarity 0.21); scryfall="When this creature enters, it fights up to one target creature you don't control.
Enrage — Whenever this crea
- oracle_text advisory -- World War Hulk: oracle_text diverges (similarity 0.17); scryfall='(As this Saga enters and after your draw step, add a lore counter. Sacrifice after III.)
I — The next red or
- oracle_text advisory -- Ghalta, Stampede Tyrant: oracle_text diverges (similarity 0.17); scryfall='Trample
When Ghalta enters, put any number of creature cards from your hand onto the battlefield.'
- oracle_text advisory -- Lyra Dawnbringer: oracle_text diverges (similarity 0.11); scryfall='Flying
First strike (This creature deals combat damage before creatures without first strike.)
Lifelink (Dam
- oracle_text advisory -- Youthful Valkyrie: oracle_text diverges (similarity 0.08); scryfall='Flying
Whenever another Angel you control enters, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Bishop of Wings: oracle_text diverges (similarity 0.10); scryfall='Whenever an Angel you control enters, you gain 4 life.
Whenever an Angel you control dies, create a 1/1 white
- oracle_text advisory -- Righteous Valkyrie: oracle_text diverges (similarity 0.15); scryfall="Flying
Whenever another Angel or Cleric you control enters, you gain life equal to that creature's toughness.
- oracle_text advisory -- Seraph Sanctuary: oracle_text diverges (similarity 0.12); scryfall='When this land enters, you gain 1 life.
Whenever an Angel you control enters, you gain 1 life.
{T}: Add {C}.
- oracle_text advisory -- Resplendent Angel: oracle_text diverges (similarity 0.11); scryfall='Flying
At the beginning of each end step, if you gained 5 or more life this turn, create a 4/4 white Angel cr
- oracle_text advisory -- Legion Angel: oracle_text diverges (similarity 0.07); scryfall='Flying
When this creature enters, you may reveal a card you own named Legion Angel from outside the game and
- oracle_text advisory -- Serra the Benevolent: oracle_text diverges (similarity 0.16); scryfall='+2: Creatures you control with flying get +1/+1 until end of turn.
−3: Create a 4/4 white Angel creature toke
- oracle_text advisory -- Giada, Font of Hope: oracle_text diverges (similarity 0.11); scryfall='Flying, vigilance
Each other Angel you control enters with an additional +1/+1 counter on it for each Angel y
- oracle_text advisory -- Saproling Burst: oracle_text diverges (similarity 0.13); scryfall='Fading 7 (This enchantment enters with seven fade counters on it. At the beginning of your upkeep, remove a fa
- oracle_text advisory -- Shroofus Sproutsire: oracle_text diverges (similarity 0.06); scryfall='Trample
Whenever a Saproling you control deals combat damage to a player, create that many 1/1 green Saprolin
- oracle_text advisory -- Slimefoot, the Stowaway: oracle_text diverges (similarity 0.05); scryfall='Whenever a Saproling you control dies, Slimefoot deals 1 damage to each opponent and you gain 1 life.
{4}: Cr
- oracle_text advisory -- Vitaspore Thallid: oracle_text diverges (similarity 0.11); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this
- oracle_text advisory -- Deathspore Thallid: oracle_text diverges (similarity 0.10); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this
- oracle_text advisory -- Sporecrown Thallid: oracle_text diverges (similarity 0.22); scryfall="Each other creature you control that's a Fungus or Saproling gets +1/+1."
- oracle_text advisory -- Tukatongue Thallid: oracle_text diverges (similarity 0.13); scryfall='When this creature dies, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- Yavimaya Sapherd: oracle_text diverges (similarity 0.06); scryfall='When this creature enters, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- Deathbloom Thallid: oracle_text diverges (similarity 0.05); scryfall='When this creature dies, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- The Mycotyrant: oracle_text diverges (similarity 0.18); scryfall='Trample
The Mycotyrant\'s power and toughness are each equal to the number of creatures you control that are
- oracle_text advisory -- Simic Growth Chamber: oracle_text diverges (similarity 0.21); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {G}{
- oracle_text advisory -- Psychotrope Thallid: oracle_text diverges (similarity 0.22); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this
- oracle_text advisory -- Concordant Crossroads: oracle_text diverges (similarity 0.01); scryfall='All creatures have haste.'
- oracle_text advisory -- Brightcap Badger: oracle_text diverges (similarity 0.12); scryfall='Each Fungus and Saproling you control has "{T}: Add {G}."
At the beginning of your end step, create a 1/1 gre
- oracle_text advisory -- Fungus Frolic: oracle_text diverges (similarity 0.10); scryfall='Create two 1/1 green Saproling creature tokens. (Then exile this card. You may cast the creature later from ex
- oracle_text advisory -- Spore Swarm: oracle_text diverges (similarity 0.06); scryfall='Create three 1/1 green Saproling creature tokens.'
- oracle_text advisory -- Saproling Migration: oracle_text diverges (similarity 0.13); scryfall='Kicker {4} (You may pay an additional {4} as you cast this spell.)
Create two 1/1 green Saproling creature to
- oracle_text advisory -- Doubling Season: oracle_text diverges (similarity 0.28); scryfall='If an effect would create one or more tokens under your control, it creates twice that many of those tokens in
- oracle_text advisory -- Parallel Lives: oracle_text diverges (similarity 0.13); scryfall='If an effect would create one or more tokens under your control, it creates twice that many of those tokens in
- oracle_text advisory -- Primal Vigor: oracle_text diverges (similarity 0.15); scryfall='If one or more tokens would be created, twice that many of those tokens are created instead.
If one or more +
- oracle_text advisory -- Beastmaster Ascension: oracle_text diverges (similarity 0.17); scryfall='Whenever a creature you control attacks, you may put a quest counter on this enchantment.
As long as this enc
- oracle_text advisory -- Thallid: oracle_text diverges (similarity 0.17); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this
- oracle_text advisory -- Thallid Shell-Dweller: oracle_text diverges (similarity 0.13); scryfall='Defender
At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters
- oracle_text advisory -- Sporesower Thallid: oracle_text diverges (similarity 0.12); scryfall='At the beginning of your upkeep, put a spore counter on each Fungus you control.
Remove three spore counters
- oracle_text advisory -- Utopia Mycon: oracle_text diverges (similarity 0.12); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this
- oracle_text advisory -- Mycoloth: oracle_text diverges (similarity 0.14); scryfall='Devour 2 (As this creature enters, you may sacrifice any number of creatures. It enters with twice that many +
- oracle_text advisory -- Lightstall Inquisitor: oracle_text diverges (similarity 0.17); scryfall='Vigilance
When this creature enters, each opponent exiles a card from their hand and may play that card for a
- oracle_text advisory -- Lyra, Archangel of Dawn: oracle_text diverges (similarity 0.06); scryfall='Flying
Whenever you gain life, put a +1/+1 counter on each Angel you control.'
- oracle_text advisory -- Sunrise Sovereign: oracle_text diverges (similarity 0.10); scryfall='Other Giant creatures you control get +2/+2 and have trample.'
- oracle_text advisory -- Stinkdrinker Daredevil: oracle_text diverges (similarity 0.11); scryfall='Giant spells you cast cost {2} less to cast.'
- oracle_text advisory -- Giant Harbinger: oracle_text diverges (similarity 0.14); scryfall='When this creature enters, you may search your library for a Giant card, reveal it, then shuffle and put that
- oracle_text advisory -- Risen Reef: oracle_text diverges (similarity 0.15); scryfall="Whenever this creature or another Elemental you control enters, look at the top card of your library. If it's
- oracle_text advisory -- Flamekin Harbinger: oracle_text diverges (similarity 0.15); scryfall='When this creature enters, you may search your library for an Elemental card, reveal it, then shuffle and put
- oracle_text advisory -- Foundation Breaker: oracle_text diverges (similarity 0.17); scryfall="When this creature enters, you may destroy target artifact or enchantment.
Evoke {1}{G} (You may cast this sp
- oracle_text advisory -- Fulminator Mage: oracle_text diverges (similarity 0.07); scryfall='Sacrifice this creature: Destroy target nonbasic land.'
- oracle_text advisory -- Pyroclasm: oracle_text diverges (similarity 0.05); scryfall='Pyroclasm deals 2 damage to each creature.'
- oracle_text advisory -- Hamletback Goliath: oracle_text diverges (similarity 0.09); scryfall="Whenever another creature enters, you may put X +1/+1 counters on this creature, where X is that creature's po
- oracle_text advisory -- Borderland Behemoth: oracle_text diverges (similarity 0.06); scryfall='Trample
This creature gets +4/+4 for each other Giant you control.'
- oracle_text advisory -- Inferno Titan: oracle_text diverges (similarity 0.13); scryfall='{R}: This creature gets +1/+0 until end of turn.
Whenever this creature enters or attacks, it deals 3 damage
- oracle_text advisory -- Surtland Flinger: oracle_text diverges (similarity 0.18); scryfall="Whenever this creature attacks, you may sacrifice another creature. When you do, this creature deals damage eq
- oracle_text advisory -- Tectonic Giant: oracle_text diverges (similarity 0.16); scryfall='Whenever this creature attacks or becomes the target of a spell an opponent controls, choose one —
• This cre
- oracle_text advisory -- Metallic Mimic: oracle_text diverges (similarity 0.18); scryfall='As this creature enters, choose a creature type.
This creature is the chosen type in addition to its other ty
- oracle_text advisory -- Smokebraider: oracle_text diverges (similarity 0.12); scryfall='{T}: Add two mana in any combination of colors. Spend this mana only to cast Elemental spells or activate abil
- oracle_text advisory -- Incandescent Soulstoke: oracle_text diverges (similarity 0.17); scryfall='Other Elemental creatures you control get +1/+1.
{1}{R}, {T}: You may put an Elemental creature card from you
- oracle_text advisory -- Fury: oracle_text diverges (similarity 0.13); scryfall='Double strike
When this creature enters, it deals 4 damage divided as you choose among any number of target c
- oracle_text advisory -- Adaptive Automaton: oracle_text diverges (similarity 0.23); scryfall='As this creature enters, choose a creature type.
This creature is the chosen type in addition to its other ty
- oracle_text advisory -- Corsair Captain: oracle_text diverges (similarity 0.18); scryfall='When this creature enters, create a Treasure token. (It\'s an artifact with "{T}, Sacrifice this token: Add on
- oracle_text advisory -- Dire Fleet Captain: oracle_text diverges (similarity 0.17); scryfall='Whenever this creature attacks, it gets +1/+1 until end of turn for each other attacking Pirate.'
- oracle_text advisory -- Goblin Tomb Raider: oracle_text diverges (similarity 0.09); scryfall='As long as you control an artifact, this creature gets +1/+0 and has haste.'
- oracle_text advisory -- Forerunner of the Coalition: oracle_text diverges (similarity 0.22); scryfall='When this creature enters, you may search your library for a Pirate card, reveal it, then shuffle and put that
- oracle_text advisory -- Staunch Crewmate: oracle_text diverges (similarity 0.33); scryfall='When this creature enters, look at the top four cards of your library. You may reveal an artifact or Pirate ca
- oracle_text advisory -- Malcolm, the Eyes: oracle_text diverges (similarity 0.26); scryfall='Flying, haste
Whenever you cast your second spell each turn, investigate. (Create a Clue token. It\'s an arti
- oracle_text advisory -- Siren Stormtamer: oracle_text diverges (similarity 0.16); scryfall='Flying
{U}, Sacrifice this creature: Counter target spell or ability that targets you or a creature you contr
- oracle_text advisory -- Daring Buccaneer: oracle_text diverges (similarity 0.09); scryfall='As an additional cost to cast this spell, reveal a Pirate card from your hand or pay {2}.'
- oracle_text advisory -- Kitesail Larcenist: oracle_text diverges (similarity 0.17); scryfall='Flying, ward {1}
When this creature enters, for each player, choose up to one other target artifact or creatu
- oracle_text advisory -- Spirebluff Canal: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped unless you control two or fewer other lands.
{T}: Add {U} or {R}.'
- oracle_text advisory -- Blackcleave Cliffs: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped unless you control two or fewer other lands.
{T}: Add {B} or {R}.'
- oracle_text advisory -- Battlefield Forge: oracle_text diverges (similarity 0.18); scryfall='{T}: Add {C}.
{T}: Add {R} or {W}. This land deals 1 damage to you.'
- oracle_text advisory -- Karplusan Forest: oracle_text diverges (similarity 0.43); scryfall='{T}: Add {C}.
{T}: Add {R} or {G}. This land deals 1 damage to you.'
- oracle_text advisory -- Tarnished Citadel: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.
{T}: Add one mana of any color. This land deals 3 damage to you.'
- oracle_text advisory -- Grand Coliseum: oracle_text diverges (similarity 0.53); scryfall='This land enters tapped.
{T}: Add {C}.
{T}: Add one mana of any color. This land deals 1 damage to you.'
- oracle_text advisory -- Ancient Tomb: oracle_text diverges (similarity 0.17); scryfall='{T}: Add {C}{C}. This land deals 2 damage to you.'
- oracle_text advisory -- Manabarbs: oracle_text diverges (similarity 0.12); scryfall='Whenever a player taps a land for mana, this enchantment deals 1 damage to that player.'
- oracle_text advisory -- Tamanoa: oracle_text diverges (similarity 0.13); scryfall='Whenever a noncreature source you control deals damage, you gain that much life.'
- oracle_text advisory -- Rhox Faithmender: oracle_text diverges (similarity 0.37); scryfall='Lifelink (Damage dealt by this creature also causes you to gain that much life.)
If you would gain life, you
- oracle_text advisory -- Vito, Thorn of the Dusk Rose: oracle_text diverges (similarity 0.16); scryfall='Whenever you gain life, target opponent loses that much life.
{3}{B}{B}: Creatures you control gain lifelink
- oracle_text advisory -- Dina, Soul Steeper: oracle_text diverges (similarity 0.21); scryfall="Whenever you gain life, each opponent loses 1 life.
{1}, Sacrifice another creature: Dina gets +X/+0 until en
- oracle_text advisory -- Bilbo, Birthday Celebrant: oracle_text diverges (similarity 0.24); scryfall='If you would gain life, you gain that much life plus 1 instead.
{2}{W}{B}{G}, {T}, Exile Bilbo: Search your l
- oracle_text advisory -- Purity: oracle_text diverges (similarity 0.24); scryfall="Flying
If noncombat damage would be dealt to you, prevent that damage. You gain life equal to the damage prev
- oracle_text advisory -- Spellshock: oracle_text diverges (similarity 0.09); scryfall='Whenever a player casts a spell, this enchantment deals 2 damage to that player.'
- oracle_text advisory -- Pyrohemia: oracle_text diverges (similarity 0.11); scryfall='At the beginning of the end step, if no creatures are on the battlefield, sacrifice this enchantment.
{R}: Th
- oracle_text advisory -- Rolling Earthquake: oracle_text diverges (similarity 0.08); scryfall='Rolling Earthquake deals X damage to each creature without horsemanship and each player.'
- oracle_text advisory -- Beseech the Queen: oracle_text diverges (similarity 0.20); scryfall="({2/B} can be paid with any two mana or with {B}. This card's mana value is 6.)
Search your library for a car
- oracle_text advisory -- Green Sun's Zenith: oracle_text diverges (similarity 0.17); scryfall="Search your library for a green creature card with mana value X or less, put it onto the battlefield, then shu
- oracle_text advisory -- Dimir House Guard: oracle_text diverges (similarity 0.25); scryfall="Fear (This creature can't be blocked except by artifact creatures and/or black creatures.)
Sacrifice a creatu
- oracle_text advisory -- Shriekmaw: oracle_text diverges (similarity 0.26); scryfall="Fear (This creature can't be blocked except by artifact creatures and/or black creatures.)
When this creature
- oracle_text advisory -- Timeless Witness: oracle_text diverges (similarity 0.29); scryfall="When this creature enters, return target card from your graveyard to your hand.
Eternalize {5}{G}{G} ({5}{G}{
- oracle_text advisory -- Acidic Slime: oracle_text diverges (similarity 0.25); scryfall='Deathtouch (Any amount of damage this deals to a creature is enough to destroy it.)
When this creature enters
- oracle_text advisory -- Ageless Entity: oracle_text diverges (similarity 0.03); scryfall='Whenever you gain life, put that many +1/+1 counters on this creature.'
- oracle_text advisory -- Selesnya Sanctuary: oracle_text diverges (similarity 0.06); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {G}{
- oracle_text advisory -- Blossoming Sands: oracle_text diverges (similarity 0.06); scryfall='This land enters tapped.
When this land enters, you gain 1 life.
{T}: Add {G} or {W}.'
- oracle_text advisory -- Verdant Sun's Avatar: oracle_text diverges (similarity 0.05); scryfall="Whenever this creature or another creature you control enters, you gain life equal to that creature's toughnes
- oracle_text advisory -- Feed the Clan: oracle_text diverges (similarity 0.04); scryfall='You gain 5 life.
Ferocious — You gain 10 life instead if you control a creature with power 4 or greater.'
- oracle_text advisory -- Nourish: oracle_text diverges (similarity 0.03); scryfall='You gain 6 life.'
- oracle_text advisory -- Eternal Witness: oracle_text diverges (similarity 0.13); scryfall='When this creature enters, you may return target card from your graveyard to your hand.'
- oracle_text advisory -- Elvish Harbinger: oracle_text diverges (similarity 0.20); scryfall='When this creature enters, you may search your library for an Elf card, reveal it, then shuffle and put that c
- oracle_text advisory -- Ezuri, Renegade Leader: oracle_text diverges (similarity 0.16); scryfall='{G}: Regenerate another target Elf.
{2}{G}{G}{G}: Elf creatures you control get +3/+3 and gain trample until
- oracle_text advisory -- Scholar of Athreos: oracle_text diverges (similarity 0.13); scryfall='{2}{B}: Each opponent loses 1 life. You gain life equal to the life lost this way.'
- oracle_text advisory -- Brindle Boar: oracle_text diverges (similarity 0.05); scryfall='Sacrifice this creature: You gain 4 life.'
- oracle_text advisory -- Weather the Storm: oracle_text diverges (similarity 0.14); scryfall='You gain 3 life.
Storm (When you cast this spell, copy it for each spell cast before it this turn.)'
- oracle_text advisory -- Utopia Sprawl: oracle_text diverges (similarity 0.21); scryfall='Enchant Forest
As this Aura enters, choose a color.
Whenever enchanted Forest is tapped for mana, its contro
- oracle_text advisory -- Crab Umbra: oracle_text diverges (similarity 0.16); scryfall='Enchant creature
{2}{U}: Untap enchanted creature.
Umbra armor (If enchanted creature would be destroyed, in
- oracle_text advisory -- Staff of Domination: oracle_text diverges (similarity 0.09); scryfall='{1}: Untap this artifact.
{2}, {T}: You gain 1 life.
{3}, {T}: Untap target creature.
{4}, {T}: Tap target
- oracle_text advisory -- Galvanic Alchemist: oracle_text diverges (similarity 0.18); scryfall='Soulbond (You may pair this creature with another unpaired creature when either enters. They remain paired for
- oracle_text advisory -- Seedcradle Witch: oracle_text diverges (similarity 0.08); scryfall='{2}{G}{W}: Target creature gets +3/+3 until end of turn. Untap that creature.'
- oracle_text advisory -- Trufflesnout: oracle_text diverges (similarity 0.15); scryfall='When this creature enters, choose one —
• Put a +1/+1 counter on this creature.
• You gain 4 life.'
- oracle_text advisory -- Heroes' Reunion: oracle_text diverges (similarity 0.01); scryfall='Target player gains 7 life.'
- oracle_text advisory -- Rest for the Weary: oracle_text diverges (similarity 0.05); scryfall='Target player gains 4 life.
Landfall — If you had a land enter the battlefield under your control this turn,
- oracle_text advisory -- Accomplished Alchemist: oracle_text diverges (similarity 0.03); scryfall='{T}: Add one mana of any color.
{T}: Add X mana of any one color, where X is the amount of life you gained th
- oracle_text advisory -- Blighted Steppe: oracle_text diverges (similarity 0.04); scryfall='{T}: Add {C}.
{3}{W}, {T}, Sacrifice this land: You gain 2 life for each creature you control.'
- oracle_text advisory -- Wellwisher: oracle_text diverges (similarity 0.02); scryfall='{T}: You gain 1 life for each Elf on the battlefield.'
- oracle_text advisory -- Nykthos Paragon: oracle_text diverges (similarity 0.03); scryfall='Whenever you gain life, you may put that many +1/+1 counters on each creature you control. Do this only once e
- oracle_text advisory -- Blossoming Bogbeast: oracle_text diverges (similarity 0.05); scryfall='Whenever this creature attacks, you gain 2 life. Then creatures you control gain trample and get +X/+X until e
- oracle_text advisory -- Genesis Wave: oracle_text diverges (similarity 0.07); scryfall="Reveal the top X cards of your library. You may put any number of permanent cards with mana value X or less fr
- oracle_text advisory -- Champion of the Parish: oracle_text diverges (similarity 0.11); scryfall='Whenever another Human you control enters, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Field Marshal: oracle_text diverges (similarity 0.12); scryfall='Other Soldier creatures get +1/+1 and have first strike. (They deal combat damage before creatures without fir
- oracle_text advisory -- Thalia, Guardian of Thraben: oracle_text diverges (similarity 0.06); scryfall='First strike
Noncreature spells cost {1} more to cast.'
- oracle_text advisory -- Thalia's Lieutenant: oracle_text diverges (similarity 0.12); scryfall='When this creature enters, put a +1/+1 counter on each other Human you control.
Whenever another Human you co
- oracle_text advisory -- Rick, Steadfast Leader: oracle_text diverges (similarity 0.16); scryfall="As Greymond, Avacyn's Stalwart enters, choose two abilities from among first strike, vigilance, and lifelink.\
- oracle_text advisory -- Esper Sentinel: oracle_text diverges (similarity 0.14); scryfall="Whenever an opponent casts their first noncreature spell each turn, draw a card unless that player pays {X}, w
- oracle_text advisory -- Coppercoat Vanguard: oracle_text diverges (similarity 0.17); scryfall='Each other Human you control gets +1/+0 and has ward {1}. (Whenever it becomes the target of a spell or abilit
- oracle_text advisory -- Recruitment Officer: oracle_text diverges (similarity 0.17); scryfall='{3}{W}: Look at the top four cards of your library. You may reveal a creature card with mana value 3 or less f
- oracle_text advisory -- Jirina, Dauntless General: oracle_text diverges (similarity 0.09); scryfall="When Jirina enters, exile target player's graveyard.
Sacrifice Jirina: Humans you control gain hexproof and i
- oracle_text advisory -- Harbin, Vanguard Aviator: oracle_text diverges (similarity 0.08); scryfall='Flying
Whenever you attack with five or more Soldiers, creatures you control get +1/+1 and gain flying until
- oracle_text advisory -- General Kudro of Drannith: oracle_text diverges (similarity 0.19); scryfall="Other Humans you control get +1/+1.
Whenever General Kudro or another Human you control enters, exile target
- oracle_text advisory -- Cathar Commando: oracle_text diverges (similarity 0.06); scryfall='Flash
{1}, Sacrifice this creature: Destroy target artifact or enchantment.'
- oracle_text advisory -- Brutal Cathar: oracle_text diverges (similarity 0.10); scryfall='Whenever this creature enters or transforms into Brutal Cathar, exile target creature an opponent controls unt
- oracle_text advisory -- Moonrage Brute: oracle_text diverges (similarity 0.22); scryfall='First strike
Ward—Pay 3 life.
Nightbound (If a player casts at least two spells during their own turn, it be
- oracle_text advisory -- King Darien XLVIII: oracle_text diverges (similarity 0.17); scryfall='Other creatures you control get +1/+1.
{3}{G}{W}: Put a +1/+1 counter on King Darien and create a 1/1 white S
- oracle_text advisory -- Fortified Beachhead: oracle_text diverges (similarity 0.26); scryfall='As this land enters, you may reveal a Soldier card from your hand. This land enters tapped unless you revealed
- oracle_text advisory -- Silent Clearing: oracle_text diverges (similarity 0.10); scryfall='{T}, Pay 1 life: Add {W} or {B}.
{1}, {T}, Sacrifice this land: Draw a card.'
- oracle_text advisory -- Waterlogged Grove: oracle_text diverges (similarity 0.11); scryfall='{T}, Pay 1 life: Add {G} or {U}.
{1}, {T}, Sacrifice this land: Draw a card.'
- oracle_text advisory -- Adelbert Steiner: oracle_text diverges (similarity 0.02); scryfall='Lifelink
Adelbert Steiner gets +1/+1 for each Equipment you control.'
- oracle_text advisory -- Adaptive Omnitool: oracle_text diverges (similarity 0.07); scryfall='Equipped creature gets +1/+1 for each artifact you control.
Whenever equipped creature attacks, look at the t
- oracle_text advisory -- Kíli the Resourceful: oracle_text diverges (similarity 0.07); scryfall='Storied (If you control three or more artifacts, legendaries, and/or Sagas, you have an enduring story for the
- oracle_text advisory -- Almost Perfect: oracle_text diverges (similarity 0.20); scryfall='Enchant creature
Enchanted creature has base power and toughness 9/10 and has indestructible.'
- oracle_text advisory -- Arcanum Wings: oracle_text diverges (similarity 0.15); scryfall='Enchant creature
Enchanted creature has flying.
Aura swap {2}{U} ({2}{U}: Exchange this Aura with an Aura ca
- oracle_text advisory -- Boseiju, Who Endures: oracle_text diverges (similarity 0.38); scryfall='{T}: Add {G}.
Channel — {1}{G}, Discard this card: Destroy target artifact, enchantment, or nonbasic land an
- oracle_text advisory -- Botanical Sanctum: oracle_text diverges (similarity 0.29); scryfall='This land enters tapped unless you control two or fewer other lands.
{T}: Add {G} or {U}.'
- oracle_text advisory -- Bruna, Light of Alabaster: oracle_text diverges (similarity 0.24); scryfall='Flying, vigilance
Whenever Bruna attacks or blocks, you may attach to it any number of Auras on the battlefie
- oracle_text advisory -- Colossification: oracle_text diverges (similarity 0.14); scryfall='Enchant creature
When this Aura enters, tap enchanted creature.
Enchanted creature gets +20/+20.'
- oracle_text advisory -- Eldrazi Conscription: oracle_text diverges (similarity 0.39); scryfall='Enchant creature
Enchanted creature gets +10/+10 and has trample and annihilator 2. (Whenever it attacks, def
- oracle_text advisory -- Glittering Wish: oracle_text diverges (similarity 0.06); scryfall='You may reveal a multicolored card you own from outside the game and put it into your hand. Exile Glittering W
- oracle_text advisory -- Indrik Umbra: oracle_text diverges (similarity 0.38); scryfall='Enchant creature
Enchanted creature gets +4/+4 and has first strike, and all creatures able to block it do so
- oracle_text advisory -- Mother of Runes: oracle_text diverges (similarity 0.13); scryfall='{T}: Target creature you control gains protection from the color of your choice until end of turn.'
- oracle_text advisory -- Mythic Proportions: oracle_text diverges (similarity 0.40); scryfall='Enchant creature
Enchanted creature gets +8/+8 and has trample.'
- oracle_text advisory -- Open the Armory: oracle_text diverges (similarity 0.21); scryfall='Search your library for an Aura or Equipment card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Prodigious Growth: oracle_text diverges (similarity 0.26); scryfall='Enchant creature
Enchanted creature gets +7/+7 and has trample.'
- oracle_text advisory -- Seaside Citadel: oracle_text diverges (similarity 0.15); scryfall='This land enters tapped.
{T}: Add {G}, {W}, or {U}.'
- oracle_text advisory -- Skycloud Expanse: oracle_text diverges (similarity 0.06); scryfall='{1}, {T}: Add {W}{U}.'
- oracle_text advisory -- Somberwald Sage: oracle_text diverges (similarity 0.12); scryfall='{T}: Add three mana of any one color. Spend this mana only to cast creature spells.'
- oracle_text advisory -- Unflinching Courage: oracle_text diverges (similarity 0.43); scryfall='Enchant creature
Enchanted creature gets +2/+2 and has trample and lifelink. (Damage dealt by the creature al
- oracle_text advisory -- Linvala, Shield of Sea Gate: oracle_text diverges (similarity 0.31); scryfall="Flying
At the beginning of combat on your turn, if you have a full party, choose target nonland permanent an
- oracle_text advisory -- Troyan, Gutsy Explorer: oracle_text diverges (similarity 0.15); scryfall='{T}: Add {G}{U}. Spend this mana only to cast spells with mana value 5 or greater or spells with {X} in their
- oracle_text advisory -- Detention Sphere: oracle_text diverges (similarity 0.25); scryfall="When this enchantment enters, you may exile target nonland permanent not named Detention Sphere and all other
- oracle_text advisory -- Steel of the Godhead: oracle_text diverges (similarity 0.31); scryfall="Enchant creature
As long as enchanted creature is white, it gets +1/+1 and has lifelink. (Damage dealt by the
- oracle_text advisory -- Auroral Procession: oracle_text diverges (similarity 0.07); scryfall='Return target card from your graveyard to your hand.'
- oracle_text advisory -- Reborn Hope: oracle_text diverges (similarity 0.10); scryfall='Return target multicolored card from your graveyard to your hand.'
- oracle_text advisory -- Rampaging Baloths: oracle_text diverges (similarity 0.14); scryfall='Trample
Landfall — Whenever a land you control enters, create a 4/4 green Beast creature token.'
- oracle_text advisory -- Mossborn Hydra: oracle_text diverges (similarity 0.13); scryfall="Trample (This creature can deal excess combat damage to the player or planeswalker it's attacking.)
This crea
- oracle_text advisory -- Scythecat Cub: oracle_text diverges (similarity 0.16); scryfall='Trample
Landfall — Whenever a land you control enters, put a +1/+1 counter on target creature you control. If
- oracle_text advisory -- Scute Swarm: oracle_text diverges (similarity 0.15); scryfall="Landfall — Whenever a land you control enters, create a 1/1 green Insect creature token. If you control six or
- oracle_text advisory -- Moraug, Fury of Akoum: oracle_text diverges (similarity 0.17); scryfall="Each creature you control gets +1/+0 for each time it has attacked this turn.
Landfall — Whenever a land you
- oracle_text advisory -- Avenger of Zendikar: oracle_text diverges (similarity 0.21); scryfall='When this creature enters, create a 0/1 green Plant creature token for each land you control.
Landfall — When
- oracle_text advisory -- Omnath, Locus of Rage: oracle_text diverges (similarity 0.20); scryfall='Landfall — Whenever a land you control enters, create a 5/5 red and green Elemental creature token.
Whenever
- oracle_text advisory -- Phylath, World Sculptor: oracle_text diverges (similarity 0.19); scryfall='When Phylath enters, create a 0/1 green Plant creature token for each basic land you control.
Landfall — When
- oracle_text advisory -- Courser of Kruphix: oracle_text diverges (similarity 0.17); scryfall='Play with the top card of your library revealed.
You may play lands from the top of your library.
Landfall —
- oracle_text advisory -- Khalni Heart Expedition: oracle_text diverges (similarity 0.19); scryfall='Landfall — Whenever a land you control enters, you may put a quest counter on this enchantment.
Remove three
- oracle_text advisory -- Akoum Hellhound: oracle_text diverges (similarity 0.15); scryfall='Landfall — Whenever a land you control enters, this creature gets +2/+2 until end of turn.'
- oracle_text advisory -- Brushfire Elemental: oracle_text diverges (similarity 0.26); scryfall="Haste
This creature can't be blocked by creatures with power 2 or less.
Landfall — Whenever a land you contr
- oracle_text advisory -- Rampant Growth: oracle_text diverges (similarity 0.16); scryfall='Search your library for a basic land card, put that card onto the battlefield tapped, then shuffle.'
- oracle_text advisory -- Search for Tomorrow: oracle_text diverges (similarity 0.26); scryfall='Search your library for a basic land card, put it onto the battlefield, then shuffle.
Suspend 2—{G} (Rather t
- oracle_text advisory -- Cultivate: oracle_text diverges (similarity 0.24); scryfall='Search your library for up to two basic land cards, reveal those cards, put one onto the battlefield tapped an
- oracle_text advisory -- Evolving Wilds: oracle_text diverges (similarity 0.11); scryfall='{T}, Sacrifice this land: Search your library for a basic land card, put it onto the battlefield tapped, then
- oracle_text advisory -- Terramorphic Expanse: oracle_text diverges (similarity 0.11); scryfall='{T}, Sacrifice this land: Search your library for a basic land card, put it onto the battlefield tapped, then
- oracle_text advisory -- Myriad Landscape: oracle_text diverges (similarity 0.25); scryfall='This land enters tapped.
{T}: Add {C}.
{2}, {T}, Sacrifice this land: Search your library for up to two basi
- oracle_text advisory -- Primeval Titan: oracle_text diverges (similarity 0.16); scryfall='Trample
Whenever this creature enters or attacks, you may search your library for up to two land cards, put t
- oracle_text advisory -- Radiant Fountain: oracle_text diverges (similarity 0.12); scryfall='When this land enters, you gain 2 life.
{T}: Add {C}.'
- oracle_text advisory -- Haunted Fengraf: oracle_text diverges (similarity 0.12); scryfall='{T}: Add {C}.
{3}, {T}, Sacrifice this land: Return a creature card at random from your graveyard to your han
- oracle_text advisory -- Temple of Abandon: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Deceit: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Enlightenment: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Malady: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Malice: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Mystery: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Plenty: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Triumph: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Zhalfirin Void: oracle_text diverges (similarity 0.74); scryfall='When this land enters, scry 1. (Look at the top card of your library. You may put that card on the bottom.)
{
- oracle_text advisory -- Crystal Grotto: oracle_text diverges (similarity 0.39); scryfall='When this land enters, scry 1.
{T}: Add {C}.
{1}, {T}: Add one mana of any color.'
- oracle_text advisory -- Rumble Arena: oracle_text diverges (similarity 0.44); scryfall='Vigilance
When this land enters, scry 1. (Look at the top card of your library. You may put it on the bottom.
- oracle_text advisory -- The Grey Havens: oracle_text diverges (similarity 0.24); scryfall='When The Grey Havens enters, scry 1.
{T}: Add {C}.
{T}: Add one mana of any color among legendary creature c
- oracle_text advisory -- Galadriel of Lothlórien: oracle_text diverges (similarity 0.24); scryfall='Whenever the Ring tempts you, if you chose a creature other than Galadriel as your Ring-bearer, scry 3.
Whene
- oracle_text advisory -- Clifftop Retreat: oracle_text diverges (similarity 0.37); scryfall='This land enters tapped unless you control a Mountain or a Plains.
{T}: Add {R} or {W}.'
- oracle_text advisory -- Furycalm Snarl: oracle_text diverges (similarity 0.31); scryfall="As this land enters, you may reveal a Mountain or Plains card from your hand. If you don't, this land enters t
- oracle_text advisory -- Steelshaper's Gift: oracle_text diverges (similarity 0.18); scryfall='Search your library for an Equipment card, reveal that card, put it into your hand, then shuffle.'
- oracle_text advisory -- Swiftblade Vindicator: oracle_text diverges (similarity 0.27); scryfall="Double strike (This creature deals both first-strike and regular combat damage.)
Vigilance (Attacking doesn't
- oracle_text advisory -- Lizard Blades: oracle_text diverges (similarity 0.11); scryfall="Double strike
Equipped creature has double strike.
Reconfigure {2} ({2}: Attach to target creature you contr
- oracle_text advisory -- Magnetic Theft: oracle_text diverges (similarity 0.04); scryfall="Attach target Equipment to target creature. (Control of the Equipment doesn't change.)"
- oracle_text advisory -- Forge Anew: oracle_text diverges (similarity 0.09); scryfall='When this enchantment enters, return target Equipment card from your graveyard to the battlefield.
During you
- oracle_text advisory -- Raubahn, Bull of Ala Mhigo: oracle_text diverges (similarity 0.07); scryfall="Ward—Pay life equal to Raubahn's power.
Whenever Raubahn attacks, attach up to one target Equipment you contr
- oracle_text advisory -- Speaker of the Heavens: oracle_text diverges (similarity 0.16); scryfall="Vigilance, lifelink (Attacking doesn't cause this creature to tap. Damage dealt by this creature also causes y
- oracle_text advisory -- Path to Exile: oracle_text diverges (similarity 0.12); scryfall='Exile target creature. Its controller may search their library for a basic land card, put that card onto the b
- oracle_text advisory -- Sulfur Falls: oracle_text diverges (similarity 0.27); scryfall='This land enters tapped unless you control an Island or a Mountain.
{T}: Add {U} or {R}.'
- oracle_text advisory -- Gitaxian Probe: oracle_text diverges (similarity 0.16); scryfall="({U/P} can be paid with either {U} or 2 life.)
Look at target player's hand.
Draw a card."
- oracle_text advisory -- Generator Servant: oracle_text diverges (similarity 0.15); scryfall='{T}, Sacrifice this creature: Add {C}{C}. If any of that mana is spent on a creature spell, it gains haste unt
- oracle_text advisory -- Brainstorm: oracle_text diverges (similarity 0.07); scryfall='Draw three cards, then put two cards from your hand on top of your library in any order.'
- oracle_text advisory -- Legion Lieutenant: oracle_text diverges (similarity 0.11); scryfall='Other Vampires you control get +1/+1.'
- oracle_text advisory -- Vampire of the Dire Moon: oracle_text diverges (similarity 0.37); scryfall='Deathtouch (Any amount of damage this deals to a creature is enough to destroy it.)
Lifelink (Damage dealt by
- oracle_text advisory -- Falkenrath Pit Fighter: oracle_text diverges (similarity 0.11); scryfall='{1}{R}, Discard a card, Sacrifice a Vampire: Draw two cards. Activate only if an opponent lost life this turn.
- oracle_text advisory -- Stromkirk Condemned: oracle_text diverges (similarity 0.15); scryfall='Discard a card: Vampires you control get +1/+1 until end of turn. Activate only once each turn.'
- oracle_text advisory -- Voldaren Estate: oracle_text diverges (similarity 0.35); scryfall='{T}: Add {C}.
{T}, Pay 1 life: Add one mana of any color. Spend this mana only to cast a Vampire spell.
{5},
- oracle_text advisory -- Bloodghast: oracle_text diverges (similarity 0.19); scryfall="This creature can't block.
This creature has haste as long as an opponent has 10 or less life.
Landfall — Wh
- oracle_text advisory -- Stromkirk Captain: oracle_text diverges (similarity 0.11); scryfall='First strike
Other Vampire creatures you control get +1/+1 and have first strike.'
- oracle_text advisory -- Drana, Liberator of Malakir: oracle_text diverges (similarity 0.14); scryfall='Flying, first strike
Whenever Drana deals combat damage to a player, put a +1/+1 counter on each attacking cr
- oracle_text advisory -- Mavren Fein, Dusk Apostle: oracle_text diverges (similarity 0.14); scryfall='Whenever one or more nontoken Vampires you control attack, create a 1/1 white Vampire creature token with life
- oracle_text advisory -- Vampire Socialite: oracle_text diverges (similarity 0.26); scryfall="Menace (This creature can't be blocked except by two or more creatures.)
When this creature enters, if an opp
- oracle_text advisory -- Falkenrath Gorger: oracle_text diverges (similarity 0.27); scryfall="Each Vampire creature card you own that isn't on the battlefield has madness. The madness cost is equal to its
- oracle_text advisory -- Markov Baron: oracle_text diverges (similarity 0.26); scryfall="Convoke (Your creatures can help cast this spell. Each creature you tap while casting this spell pays for {1}
- oracle_text advisory -- Asylum Visitor: oracle_text diverges (similarity 0.33); scryfall="At the beginning of each player's upkeep, if that player has no cards in hand, you draw a card and you lose 1
- oracle_text advisory -- Captivating Vampire: oracle_text diverges (similarity 0.12); scryfall='Other Vampire creatures you control get +1/+1.
Tap five untapped Vampires you control: Gain control of target
- oracle_text advisory -- Bloodline Keeper: oracle_text diverges (similarity 0.12); scryfall='Flying
{T}: Create a 2/2 black Vampire creature token with flying.
{B}: Transform this creature. Activate on
- oracle_text advisory -- Lord of Lineage: oracle_text diverges (similarity 0.17); scryfall='Flying
Other Vampire creatures you control get +2/+2.
{T}: Create a 2/2 black Vampire creature token with fl
- oracle_text advisory -- Kessig Wolf Run: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {C}.
{X}{R}{G}, {T}: Target creature gets +X/+0 and gains trample until end of turn.'
- oracle_text advisory -- Harrow: oracle_text diverges (similarity 0.19); scryfall='As an additional cost to cast this spell, sacrifice a land.
Search your library for up to two basic land card
- oracle_text advisory -- Explosive Vegetation: oracle_text diverges (similarity 0.29); scryfall='Search your library for up to two basic land cards, put them onto the battlefield tapped, then shuffle.'
- oracle_text advisory -- Nature's Lore: oracle_text diverges (similarity 0.19); scryfall='Search your library for a Forest card, put that card onto the battlefield, then shuffle.'
- oracle_text advisory -- Skyshroud Claim: oracle_text diverges (similarity 0.26); scryfall='Search your library for up to two Forest cards, put them onto the battlefield, then shuffle.'
- oracle_text advisory -- Martyr of Sands: oracle_text diverges (similarity 0.06); scryfall='{1}, Reveal X white cards from your hand, Sacrifice this creature: You gain three times X life.'
- oracle_text advisory -- Walking Ballista: oracle_text diverges (similarity 0.06); scryfall='This creature enters with X +1/+1 counters on it.
{4}: Put a +1/+1 counter on this creature.
Remove a +1/+1
- oracle_text advisory -- Abiding Grace: oracle_text diverges (similarity 0.11); scryfall='At the beginning of your end step, choose one —
• You gain 1 life.
• Return target creature card with mana v
- oracle_text advisory -- Emeria, the Sky Ruin: oracle_text diverges (similarity 0.19); scryfall='This land enters tapped.
At the beginning of your upkeep, if you control seven or more Plains, you may return
- oracle_text advisory -- Brainsurge: oracle_text diverges (similarity 0.09); scryfall='Draw four cards, then put two cards from your hand on top of your library in any order.'
- oracle_text advisory -- Street Wraith: oracle_text diverges (similarity 0.26); scryfall="Swampwalk (This creature can't be blocked as long as defending player controls a Swamp.)
Cycling—Pay 2 life.
- oracle_text advisory -- Unpredictable Cyclone: oracle_text diverges (similarity 0.31); scryfall="If a cycling ability of another nonland card would cause you to draw a card, instead exile cards from the top
- oracle_text advisory -- Curator of Mysteries: oracle_text diverges (similarity 0.16); scryfall='Flying
Whenever you cycle or discard another card, scry 1.
Cycling {U} ({U}, Discard this card: Draw a card.
- oracle_text advisory -- Emrakul, the Aeons Torn: oracle_text diverges (similarity 0.17); scryfall="This spell can't be countered.
When you cast this spell, take an extra turn after this one.
Flying, protecti
- oracle_text advisory -- Ulamog, the Ceaseless Hunger: oracle_text diverges (similarity 0.15); scryfall='When you cast this spell, exile two target permanents.
Indestructible
Whenever Ulamog attacks, defending pla
- oracle_text advisory -- Ulamog, the Infinite Gyre: oracle_text diverges (similarity 0.31); scryfall='When you cast this spell, destroy target permanent.
Indestructible
Annihilator 4 (Whenever this creature att
- oracle_text advisory -- Long-Term Plans: oracle_text diverges (similarity 0.10); scryfall='Search your library for a card, then shuffle and put that card third from the top.'
- oracle_text advisory -- Toxic Deluge: oracle_text diverges (similarity 0.13); scryfall='As an additional cost to cast this spell, pay X life.
All creatures get -X/-X until end of turn.'
- oracle_text advisory -- Lord of Atlantis: oracle_text diverges (similarity 0.47); scryfall="Other Merfolk get +1/+1 and have islandwalk. (They can't be blocked as long as defending player controls an Is
- oracle_text advisory -- Master of the Pearl Trident: oracle_text diverges (similarity 0.63); scryfall="Other Merfolk creatures you control get +1/+1 and have islandwalk. (They can't be blocked as long as defending
- oracle_text advisory -- Vodalian Hexcatcher: oracle_text diverges (similarity 0.39); scryfall='Flash
Other Merfolk you control get +1/+1.
Sacrifice a Merfolk: Counter target noncreature spell unless its
- oracle_text advisory -- Phantasmal Image: oracle_text diverges (similarity 0.58); scryfall='You may have this creature enter as a copy of any creature on the battlefield, except it\'s an Illusion in add
- oracle_text advisory -- Cosi's Trickster: oracle_text diverges (similarity 0.38); scryfall='Whenever an opponent shuffles their library, you may put a +1/+1 counter on this creature.'
- oracle_text advisory -- Sink into Stupor: oracle_text diverges (similarity 0.58); scryfall="Return target spell or nonland permanent an opponent controls to its owner's hand."
- oracle_text advisory -- Dark Ritual: oracle_text diverges (similarity 0.04); scryfall='Add {B}{B}{B}.'
- oracle_text advisory -- Demonic Tutor: oracle_text diverges (similarity 0.21); scryfall='Search your library for a card, put that card into your hand, then shuffle.'
- oracle_text advisory -- Diabolic Intent: oracle_text diverges (similarity 0.25); scryfall='As an additional cost to cast this spell, sacrifice a creature.
Search your library for a card, put that card
- oracle_text advisory -- Culling the Weak: oracle_text diverges (similarity 0.13); scryfall='As an additional cost to cast this spell, sacrifice a creature.
Add {B}{B}{B}{B}.'
- oracle_text advisory -- Lake of the Dead: oracle_text diverges (similarity 0.14); scryfall="If this land would enter, sacrifice a Swamp instead. If you do, put this land onto the battlefield. If you don
- oracle_text advisory -- Profane Tutor: oracle_text diverges (similarity 0.27); scryfall='Suspend 2—{1}{B} (Rather than cast this card from your hand, pay {1}{B} and exile it with two time counters on
- oracle_text advisory -- Shadowborn Apostle: oracle_text diverges (similarity 0.17); scryfall='A deck can have any number of cards named Shadowborn Apostle.
{B}, Sacrifice six creatures named Shadowborn A
- oracle_text advisory -- Abhorrent Overlord: oracle_text diverges (similarity 0.24); scryfall='Flying
When this creature enters, create a number of 1/1 black Harpy creature tokens with flying equal to you
- oracle_text advisory -- Reaper from the Abyss: oracle_text diverges (similarity 0.15); scryfall='Flying
Morbid — At the beginning of each end step, if a creature died this turn, destroy target non-Demon cre
- oracle_text advisory -- Sire of Insanity: oracle_text diverges (similarity 0.14); scryfall='At the beginning of each end step, each player discards their hand.'
- oracle_text advisory -- Jetmir, Nexus of Revels: oracle_text diverges (similarity 0.40); scryfall='Creatures you control get +1/+0 and have vigilance as long as you control three or more creatures.
Creatures
- oracle_text advisory -- Thrumming Stone: oracle_text diverges (similarity 0.17); scryfall='Spells you cast have ripple 4. (Whenever you cast a spell, you may reveal the top four cards of your library.
- oracle_text advisory -- Chariot of Victory: oracle_text diverges (similarity 0.45); scryfall='Equipped creature has first strike, trample, and haste.
Equip {1}'
- oracle_text advisory -- Voyaging Satyr: oracle_text diverges (similarity 0.13); scryfall='{T}: Untap target land.'
- oracle_text advisory -- Surrak and Goreclaw: oracle_text diverges (similarity 0.63); scryfall='Trample
Other creatures you control have trample.
Whenever another nontoken creature you control enters, put
- oracle_text advisory -- Nylea, God of the Hunt: oracle_text diverges (similarity 0.54); scryfall="Indestructible
As long as your devotion to green is less than five, Nylea isn't a creature. (Each {G} in the
- oracle_text advisory -- Extraplanar Lens: oracle_text diverges (similarity 0.51); scryfall='Imprint — When this artifact enters, you may exile target land you control.
Whenever a land with the same nam
- oracle_text advisory -- Horn of Greed: oracle_text diverges (similarity 0.27); scryfall='Whenever a player plays a land, that player draws a card.'
- oracle_text advisory -- Howling Mine: oracle_text diverges (similarity 0.37); scryfall="At the beginning of each player's draw step, if this artifact is untapped, that player draws an additional car
- clause_ledger: no dedicated per-clause artifact. Its function -- every oracle clause modeled/inert/deferred -- is covered by coverage(partial hard-stop) + bracket-note deferrals + viewer oracle cross-check + audit_card_fields oracle-diff. A dedicated ledger is deferred (high per-card cost, marginal added rigor).
- play advisory -- seed 60001 gi 0: turn 5 plan 3: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 8: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 20: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 21: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 22: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 23: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 24: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 33: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 45: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 58: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: phantom plan re-offered at turn 5: dropped ['Myriad Landscape (activation)'] (Myriad Landscape's ability costs {2}; only 1 mana available) -- driver p
- play advisory -- seed 60001 gi 0: turn 5 plan 0: casts 'Temporal Manipulation' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 3: casts 'Temporal Manipulation' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 4: casts 'Temporal Manipulation' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 5: casts 'Temporal Manipulation' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 9: casts 'Temporal Manipulation' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 13: casts 'Temporal Manipulation' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 0: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 1: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 2: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 3: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 8: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 9: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 10: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 11: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 20: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 21: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 22: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 23: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 24: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 37: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 38: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 39: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 40: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 41: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 42: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 52: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 53: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 54: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 55: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 56: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 57: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 71: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 78: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 90: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 97: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 0: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 4: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 5: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 6: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 11: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 12: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 13: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 14: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 30: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 31: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 32: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 33: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 34: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 35: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 36: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 37: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 38: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 39: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 64: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 65: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 66: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 67: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 68: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 69: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 70: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 71: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 72: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 73: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 96: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 107: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 128: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 139: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 0: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 1: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 2: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 3: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 8: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 9: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 10: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 11: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 12: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 13: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 14: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 15: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 25: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 26: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 27: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 28: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 29: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 39: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 40: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 41: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 42: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 43: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 58: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 64: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 4: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 5: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 11: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 12: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 26: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 27: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 39: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 40: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 41: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 42: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 46: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 47: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 48: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 49: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 50: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 51: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 52: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 53: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 69: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 70: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 71: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 72: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 73: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 74: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 75: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 76: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 96: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 97: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 98: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 99: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 100: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 101: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 102: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 103: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 104: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 105: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 106: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 107: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 108: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 109: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 110: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 111: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 112: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 113: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 137: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 143: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 144: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 145: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 193: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 199: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 200: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 201: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 234: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 240: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 241: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 242: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 283: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 294: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 314: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 325: casts 'Walk the Aeons' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 0: casts 'Emrakul, the Aeons Torn' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 1: casts 'Emrakul, the Aeons Torn' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 2: casts 'Emrakul, the Aeons Torn' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 3: casts 'Emrakul, the Aeons Torn' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 3: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 6: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 12: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 13: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 14: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 15: casts 'Nexus of Fate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 16: casts 'Nexus of Fate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 17: casts 'Nexus of Fate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 18: casts 'Nexus of Fate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 18: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 19: casts 'Nexus of Fate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 19: casts 'Nexus of Fate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 22: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 23: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 24: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 25: casts 'Nexus of Fate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 26: casts 'Nexus of Fate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 27: casts 'Nexus of Fate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 28: casts 'Nexus of Fate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 28: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 29: casts 'Nexus of Fate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 29: casts 'Nexus of Fate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 35: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 36: casts 'Nexus of Fate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 40: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 41: casts 'Nexus of Fate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 0: casts 'Emrakul, the Aeons Torn' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 1: casts 'Emrakul, the Aeons Torn' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 28: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 29: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 50: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 51: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 52: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 53: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 54: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 55: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 56: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 57: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 58: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 59: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 72: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 73: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 74: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 75: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 76: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 77: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 78: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 79: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 80: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 81: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 96: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 97: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 103: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 104: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 115: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 116: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 122: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 123: casts 'Expropriate' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 0: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 4: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 5: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 6: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 10: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 11: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 12: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 18: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 19: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 20: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 24: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 25: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 26: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 32: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 36: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 43: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- play advisory -- seed 60001 gi 0: turn 5 plan 47: casts 'Time Warp' not in hand/retrace_gy (cascade/vial/staged? -> verify)
- claude_sweep record found but no 'flags: N unresolved' line -- add one so the gate can enforce cleanliness.

<!-- verify_deck:end -->


## Implementation and measurement notes

Deck: `decks/Extra Turns/Extra Turns.cod` (60 cards; imported 2026-11-06).

## Coverage

Stage 1 ran on 2026-11-06. Missing: Walk the Aeons, Time Warp, Extraplanar Lens,
Temporal Mastery, Nexus of Fate, Horn of Greed, Howling Mine, Temporal Manipulation,
Part the Waterveil, Expropriate. Existing: Emrakul, the Aeons Torn; Ulamog, the
Ceaseless Hunger; Myriad Landscape; Snow-Covered Island; Mind Stone. No sideboard
or fetch hold review.

## Work in progress

The four extra-turn sorceries were fetched from Scryfall and added with exact Oracle
text and printed mana costs. `extra_turns_on_resolve` now grants a turn only when the
spell resolves, in both the executor and lookahead. The deck trait stamp recognizes
resolving extra-turn spells as well as Emrakul's cast trigger. Temporal Mastery's
exile on resolution uses the existing `exiles_self_on_resolve` path.

Walk the Aeons buyback work is in progress: the cast menu has an optional buyback
variant for every physical triple of Islands, the cost sacrifices the pinned three
Islands during casting, and a successfully resolving bought-back copy returns to
hand. Human play uses one buyback menu option and the existing sacrifice chooser for
the three Island picks. This still needs lockstep and decision-surface verification.

**Open implementation gaps, not accepted deferrals:**

- Temporal Mastery's `Miracle {1}{U}` is parsed but does not yet create the optional
  reveal/trigger/immediate cast window on the first draw of a turn. The draw paths
  include `GameEngine::DrawStep`, the rollout's draw-step twin, and
  `HandEntry::DrawIntoHand` for effect draws. A normal main-phase discounted cast
  would be illegal after passing the miracle window, so it is not used.

### Ulamog attack trigger integration

Ulamog, the Ceaseless Hunger's exact Scryfall Oracle attack trigger is now
`attack_exile_defender_top 20`. At declare attackers, the shared helper exiles up
to twenty cards from the passive defender's library before combat damage in both
executor and rollout. The helper uses `opponentdeck::TakeFromTop`, which marks an
emptied library as a same-turn deck-out win under the passive opponent model.
`DeckTouchesOpponentZones` now deals a real opponent library whenever Ulamog is
in either deck zone. The existing sim/dominance keys already include the opponent
library's size and order when dealt, plus `opponent_decked`; no new state field is
needed. The trigger is mandatory and has no player choice or viewer prompt. A
focused scenario starts with Ulamog, the opponent at 21 life, and twenty library
cards, requiring attack exile for the round-3 win. A unit case checks exactly
twenty exiled cards, the deck-out flag, and the non-attack control. `./build.sh`
passed; the focused unit case passed (29 assertions), and the scenario won in
round 3 with the opponent still at 11 life. Coverage-only reports no missing
cards and Ulamog full. The static viewer audit classifies this trigger as
automatic; its sole remaining self-guard finding is Temporal Mastery's separate
`miracle_cost` choice.
- Time Warp and Walk the Aeons target either player. The second integrator added
  an ordered LIFO extra-turn queue to `GameState` (with the existing own-turn count
  retained for old scenario states), opponent-target variants in `CollectActions`,
  an executor cast-time target picker, and repeated passive opponent
  upkeep/draw/end-step events at each queued opponent turn in both executor and
  rollout. Target choice is carried by `Action::player_target` through the cast
  and resolution paths; a queued own turn still leaves the round number fixed.
  `extra_turn_order` and the opponent count are folded into sim/dominance keys.
  `extra_turns_time_warp_opponent_target.json` tests the meaningful opponent
  target: it must win in the current round by exhausting a one-card opponent
  library through an extra turn and then the scheduled regular opponent turn.
  The `test_cyclone_phase2.cpp` queue test pins reverse creation order. Build
  passed; the opponent-target scenario wins round 3, while the identical fixture
  with Time Warp removed wins round 4. The existing own-target Time Warp and Walk
  buyback scenarios pass. The queue unit test passes (11 assertions), and the
  Cyclone phase 2 group passes (16 tests, 1004 assertions). Scheduled passive
  opponent creature spawns are keyed to rounds in the existing opponent model
  and materialized at our regular untap, so an opponent extra turn does not
  duplicate a spawn scheduled for that round. That remains an explicit model
  assumption, distinct from the repeated upkeep/draw/end-step events.

No profile or simulation result is valid until these gaps and the remaining six
cards are implemented and coverage is clean. Review existing bracket notes against
this deck during integration.

### Nexus, Part the Waterveil, and Expropriate integration

Nexus of Fate is an instant `{5}{U}{U}`. Its resolved extra turn uses the shared
`extra_turns_on_resolve` path. Its graveyard replacement uses
`graveyard_replace_shuffle_library` on cleanup discard and on both resolved-spell
destinations, preserving the physical card ID before the deterministic shuffle.
This deck has no library mill or other route that moves Nexus into a graveyard.
Other engine card pools can move cards to graveyards through direct push sites that
are not centralized; extending Nexus's "from anywhere" replacement to those sites
is a cross-deck gap, not an approved deferral.

Part the Waterveil has printed cost `{4}{U}{U}` and awaken 6 at `{6}{U}{U}{U}`.
The ordinary and awaken casts are separate search and human-play plan variants;
each legal owned land is an awaken variant. The chosen land is checked at cast
and resolution. If the only target is illegal at resolution, the spell grants no
extra turn and goes to graveyard rather than exiling itself. A successful awaken
turns that battlefield land into a persistent 0/0 Elemental creature with haste,
retains the land type, and puts six +1/+1 counters on it. The printed mana value
remains 6 on both casts. A normal successful cast grants the turn and exiles Part.

Expropriate has printed cost `{7}{U}{U}`. The caster's time or money vote is a
searched plan variant and visible in the play viewer. For the passive opponent,
the current provisional policy is **MONEY**: it minimizes extra turns granted to
the caster. The caster's choice of the opponent voter's owned permanent is also a
searched and viewer-visible plan variant. It is a non-targeting choice, so
shroud/hexproof do not exclude candidates; control changes without untapping.
The caster's own money vote is state-inert in this deck because the passive
opponent never takes control of caster-owned permanents. This passive-opponent
policy and own-money collapse must be disclosed in Stage 6; they are not approved
deferrals. The engine models one opposing voter even when a two-headed-giant life
total is selected, which is an open multiplayer modeling gap. If the preselected
opponent-owned permanent leaves before resolution, a remaining owned permanent is
chosen then. Ownership and tapped state are preserved.

Focused verification for these cards: five unit cases cover Nexus resolution and
discard physical identity, Part awaken permanence and illegal-target fizzle, and
Expropriate own TIME/opponent MONEY control. Four scenarios cover their searched
cast paths, including Part awaken attacking and Expropriate exile. `./build.sh`
completed after the integration. The static viewer audit classifies their choices
and reports only Temporal Mastery's separate unimplemented `miracle_cost`.

## Verification

`./build.sh` completed on 2026-11-06. Focused scenarios passed:
`extra_turns_time_warp_resolution.json` (Time Warp grants a resolved extra turn,
win in round 3) and `extra_turns_walk_buyback_chain.json` (Walk buys back, returns
to hand, recasts in the first extra turn, win in round 3). The Temporal Mastery
miracle fixture fails as expected, win round 4 instead of 3, until its immediate
draw-time cast is implemented. `git diff --check` is clean.

The static viewer audit recognizes Walk's `buyback_sacrifice_islands` as the
existing `sacrifice` choice, but the deck-wide audit still fails on
`miracle_cost`, deliberately left unclassified until the miracle choice is wired.
The target-player extra-turn choice is implemented and focused verification passed.

### Permanent integration

Extraplanar Lens, Horn of Greed, and Howling Mine now have exact Oracle entries and
parameter-backed rules paths. Lens has searched imprint choices over distinct own
land states, including decline, and stores each copy's exiled land name. Its
mana bonus is credited in the pool projection and both payment paths; the bonus
from a Snow-Covered Island is ordinary blue mana, while the land's own blue is
snow mana. The play viewer reuses its optional board choice with Lens-specific
imprint wording. Horn draws only when a land is **played** through the shared
land-drop helper, including the fetch and replacement branches. Mine counts
untapped copies on each draw step in the executor and rollout, and on each
passive opponent turn when an opponent library is dealt. Horn and Mine use the
common draw entry for the later Temporal Mastery miracle window.

Myriad Landscape's maximal-pick dominance is unsound in this deck: removing
both basics from a library can make an extra-turn draw engine deck us sooner.
The card now requests all zero-, one-, and two-basic autonomous search outcomes;
the physical search still shuffles even when zero is chosen. Other land searches
retain their existing maximal-pick behavior. The human's existing per-land
`tutor_etb` prompts still allow stopping early. The passive opponent model does
not play lands or tap lands for mana, so Horn/Lens have no opponent-land events
to observe in goldfish games; this is a model boundary, not a card-specific rule.

`./build.sh` and the six focused permanent unit cases pass, including Lens's
snow/ordinary mana split and direct payment, Horn play versus put, Mine tapped
and opponent draws, and Myriad's zero/one/two picks. The three Myriad named
unit cases pass. `extra_turns_horn_play_draw.json` and
`extra_turns_mine_draw_step.json` win in round 3 by drawing Time Warp; removing
the corresponding artifact from either fixture shifts the win to round 4.
The static viewer audit now classifies Temporal Mastery's paid miracle choice.

### Temporal Mastery miracle

The first actual draw each turn records its exact card number in `HandEntry.h`.
The pending offer resolves at the draw window in the executor and rollout:
ordinary draw step; Mind Stone and other direct digs; Horn of Greed and Howling
Mine; and spell-effect draws. On acceptance, `ResolvePendingMiracle` checks and
pays the alternate `{1}{U}` cost, emits a reveal, and casts that exact card
through the ordinary stack and cast-trigger path. Mastery's printed self-exile
then applies on resolution. The offer is consumed at once, so no later
main-phase discount exists. `BoundaryFan` searches accept/decline on the next
turn's draw; both main phases fan these answers into first draws from land
triggers, activations, and spell effects. Human play uses the paid `free_cast`
hand-zone prompt at the draw event.

Stage 2d review caught a shuffle gap: the initial plan fan and boundary fan
had checked only the pre-plan top card. Myriad Landscape can shuffle a buried
Mastery onto the top before Mind Stone or another effect draws, so the offer
could resolve with no searched pin. Both fans now consider any miracle card in
the live library. The focused unit starts with an Island on top and Mastery
second, proves the search emits both answer pins, then reorders the library as
a shuffle can and verifies that decline leaves Mastery in hand while acceptance
pays, casts, and exiles the exact copy. Plans with no miracle first draw converge
after applying; a deck with no Mastery left in library does not fan.

Five miracle fixtures pass: normal draw step, on-play Mind Stone first draw,
on-play Horn first draw, declining a losing extra turn, and Howling Mine's
second draw being ineligible. Mine remains after the ordinary draw under CR
504.1. `./build.sh` and `audit_viewer_decisions.py --no-sweep` passed on
2026-11-07. The dynamic viewer sweep needs the deck profile.

### Card data audits (2026-11-07)

`python3 scripts/audit_card_costs.py` compared 559 costed cards with Scryfall;
all 559 printed mana costs matched. The field snapshot was refreshed with the
eight new non-token card records, bringing it to 712 cards. The first bulk
refresh encountered HTTP 429 on those eight records; a slower targeted retry
fetched all eight. The subsequent full offline field audit compared all 712
snapshot cards and passed every hard field (cost, power/toughness, types, and
keywords). `audit_card_fields.py` now recognizes buyback, miracle, and awaken as
structurally modeled keywords, and imprint and council's dilemma as ability
words. `git diff --check` is clean.

The corrected baseline profile was generated at depth 0 after the default
depth-5 card-score pass proved too expensive. Extra Turns has no regression
suite rows or accepted ground truth: the proposed smoke, regression, and
overnight rows were removed together after the cost screen below. A value
leaf or exhaustive mulligan profile must wait for an affordable suite entry.

### Search cost investigation

The first corrected depth-5, 1,000-game card-score pass was stopped after
about fifteen minutes with no profile produced. A single 12-round seed also
remained active for several minutes at depth 5 and depth 3, so the existing
search cost may exceed the suite's 3× Hinata admission limit. Depth 0 completed
in under a second and was unwon on that seed. The first concrete explosion was
Walk the Aeons buyback: casting with ten Islands offered 120 physical triples
even if their permanents were otherwise identical. The autonomous cast menu
now folds triple choices by the full, existing self-removal permanent-state key,
keeping one representative for each distinct combination of sacrifice states.
This preserves choices between tapped, animated, attached, or otherwise
different Islands while reducing identical-copy duplication. The fold is
awaiting a rebuilt timing probe and must pass the suite cost and outcome gates
before the provisional rows are admitted.

The Release rebuild passed. On seed 60001 the revised depth-1, budget-5 game
finished in 18 seconds and won on round 5; the prior run had not completed
after more than a minute. Depth-5, budget-20 still took 114.8 seconds and
used 1,537,275 search units on that seed, so the fold alone does not meet the
cost gate. A reproducible coarse baseline profile was generated with
`MTG_ANALYZE_DEPTH=0`, analyzer seed 20261107, and the fixed 1,000-game
card-score sample; `min_playable` was set to 1 for this mono-blue deck. The
analyzer's optional 400-game single-thread discard lab was stopped after it
wrote the profile. Card scores are calibrated at depth 0 because the default
depth-5 generation did not complete in the available analysis window; this
limitation requires disclosure.

The engine's zero-game batch header resolves the profile to
`provider=ExtraTurns` at depth 1, budget 10. This is the intended deck-owned
provider and its certificate is `NotAssessed`; no foreign provider's narrowing
policy is inherited. The full one-game provider audit was impractical while
play cost remains high, so the routing header was checked directly. The
dynamic eight-seed play-viewer audit passed: `flicker`, `free_cast`,
`sacrifice`, and `tutor_etb` surfaced in the sweep, and the targeted seed
search verified Ulamog's `target` choice. No oracle-text choice phrase was
unmodeled.

The four-game mechanical `play_invariants.py` sweep (seed base 8001,
204 decisions) passed with no hard flags or advisories after the harness
learned the already-wired `flicker` decision type. The regression scenario
sanity gate initially failed one old Cyclone fixture because Ulamog's new
attack exile put one opponent-library Island into the shared exile zone in
addition to the two own Islands exiled by its cast trigger. The fixture now
expects three Island cards in exile on that fixed seed and passes directly;
the subsequent regression preflight cleared scenario sanity and entered its
batch run.

### Suite admission and search cost (2026-11-07)

A same-run regression cost attempt with Hinata and Extra Turns at depth 5,
budget 1 ms was stopped once the outcome was clear. Hinata's most expensive
searched case completed 150 games in 722,447 core-ms (4,816 ms/game), so the
3× limit was 14,449 ms/game. Thirteen logged Extra Turns games in its 150-game
case alone consumed 2,148,575 core-ms, or 14,324 ms per scheduled game before
counting the other 137 games. Earlier completed Extra Turns games and the
remaining long games make that case exceed the limit. One budget-1 game took
247,256 ms. The full cost gate did not complete, so this is a conservative
lower-bound rejection, not a recorded `suite_gate.py` pass/fail measurement.

A depth-1 diagnostic of seed 2005, game index 3, took 75.9 seconds and reached
1,914,313 search units over 27 real decisions, mostly repeated extra turns in
round 6. Candidate scoring was 65% of units and 97.5% of units were spent after
the nominal decision budget. The existing `MTG_DECISION_WORK_X=1` ceiling cut
that game's work to 81,534 units, but a 30-game depth-5 sample with the ceiling
still had 15 slow games totaling at least 1,287,206 core-ms: 42,907 ms per
scheduled game even before the other 15. This was rejected as a shipped policy;
the provisional depth-5 budget-1 sidecar was removed. The profile remains a
depth-0 baseline, and the deck is blocked from suite admission by search cost.

### Claude-play finding and opponent land mana (2026-11-07)

One Claude-driven game on seed 60001 won in round 4 by chaining extra turns,
Expropriate, Lens, and Emrakul, versus round 5 for the autonomous depth-5,
budget-1 search. The oracle found no confirmed illegal transitions, but it
identified a possible missing Time Warp after Expropriate took an `Opponent
Basic Land`. Those opponent markers had no card definition, so a gained basic
land supplied no mana. The marker now has a conservative Wastes-like `{T}:
Add {C}` definition, keeping its opponent identity and not assigning an
unknown colored subtype. This makes it a usable generic mana source under
the deck's control. The same choice prefix reaches a main-phase menu with
the controlled marker showing `taps: C` and Time Warp plans present. The
original full choice CSV changes meaning after that correction, so the
oracle's round-4 line requires a fresh replay before being treated as a
confirmed search regression. Lens imprint appears under the existing viewer
`flicker` decision type; its note and choices are correct, but the type label
is semantically broad.

The opponent's three land markers also previously shared card number zero, so
Expropriate's physical MONEY target choice always found the first one. The
goldfish setup now gives these markers distinct numbers when Expropriate is in
the deck; a viewer replay confirms 600000, 600001, and 600002 on seed 60001.
This widens the searched choice to all three lands while leaving the abstract
opponent land pattern unchanged for unrelated decks.

An independent Claude review replayed two possible payment issues from seeds
2003 and 2004. Both Walk the Aeons casts in the alleged partial plan actually
resolved; a third Walk was drawn by Horn of Greed during that plan. Lens mana
over-production correctly floats for the rest of the phase and is spendable.
The payer can nevertheless tap an additional Mind Stone or Landscape while
floating one blue mana, even where an exact payment for the current spell
exists. This is legal but can lose a later tap ability across a phase boundary;
it did not change either sampled game outcome. It remains a low-severity
payment-choice issue for a separately measured fix, not a card rules gap.

### Corrected-binary verification and keep probe

The rebuilt engine passed all 445 scenario fixtures. `verify_deck.py` with
seed 60001 and one game per mismatch arm passed the 40-game viewer audit,
both nonconvergence/full-depth-oracle arms, and the 174-decision mechanical
play-invariant replay. It reports failure overall because the deck has no
regression tiers or accepted ground truth, and records the Claude sweep as
pending. The independent full Scryfall cost audit already matched 559/559
costed cards; the verify run intentionally used `--no-network`. The reference
safety scan found all 24 checkout/worktree reference sets protected.

The corrected Claude replay again won seed 60001 in round 4 against the
autonomous depth-5, budget-1 round-5 result. A second Claude game won seed
2002 in round 4 by keeping a five-land hand the static curve rule would
mulligan. Turning off that rule was tested on paired depth-0 blocks: in
1,000 games it improved 16 and worsened 11 (mean -0.014 turns); in a separate
5,000 it improved 63 and worsened 70 (mean +0.0088 turns). The broad rule
change was rejected. The original profile's 1,000-game depth-0 baseline
averaged 8.557 rounds to win, counting an unwon game as 9 at the round-8
horizon. This is not a searched-depth or suite result.

### Final Horn order and play-history repairs

The Claude game at seed 2004 found that a Horn of Greed in hand could not be
cast before the turn's land drop in the same offered plan. The runner now marks
the deck as needing its land-position axis when Horn is present, and the
solver treats casting Horn as a watcher that may precede the land. A focused
unit case enumerates and applies that exact line, confirming the land drop
draws Time Warp. The final depth-0 profile was regenerated on this engine
using seed 20261107; 1,000 baseline games averaged 8.528 rounds at the
round-8 horizon, with 138 wins and 862 unwon games. This depth-0 result is
only a cheap baseline: it does not estimate the deck's searched win rate.

The seed-2007 Claude replay also exposed a viewer history omission: draws
from Howling Mine and Horn reached the hand, but the `drew` field only showed
the normal draw-step card. Both draw sites now append the drawn card to the
real-play history sink; a unit case checks the two names. The game state and
draw counts were already correct.

## Claude-play sweep

Ten Claude games on the binary before these final Horn and draw-history
repairs covered seeds 60001 and 2002–2010. Nine won in rounds 4–6, while
seed 2008 did not win by the round-8 horizon. The only substantive missing
legal line was the Horn-before-land order, now repaired. Seed 2007's
draw-history flag was also repaired; the Lens prompt's `flicker` label remains
cosmetically broad. No game found a confirmed rules or state-transition error
after review. A 15-seed sweep attempted on November 7 was blocked by Claude
CLI's weekly usage limit. The November 10 continuation below supersedes that
attempted sweep.

### Final-binary continuation (2026-11-10)

- commit: `17a1dd055`
- seeds: 62008, 63003, 64001, 64002; games: 4
- flags: 0 unresolved

Claude itself played all four games on the same corrected Release binary. Its win
rounds versus the depth-5, 20-ms AI were 5/5, 5/6, 4/4, and 4/4 respectively.
The fresh 64001 and 64002 games and the corrected 63003 game found no rules or
state-transition errors. Seed 63003's earlier win came from keeping six cards
that the AI mulliganed, but Claude had six-card draw foresight; this is a weak
keep-policy hypothesis, not a confirmed AI error.

The preceding nine-game discovery sweep (62001–62009) ran on the older binary.
It found two confirmed issues: seed 62003 carried one surplus Lens-produced
blue mana from a miracle in the draw step into main 1, and seed 62008 tapped
the targeted 6/6 haste Island to pay Part the Waterveil while five other
Lens-boosted Islands could pay both Part and Time Warp. A later exploratory
63003 game also spent an already-awakened land on ordinary spells despite
sufficient other mana. Commit `17a1dd055` clears floating mana at the draw-step
boundary in both executor and rollout, reserves the awaken target while paying
the awaken spell when alternatives exist, and ranks an attack-ready awakened
land behind ordinary mana sources on later casts. The seed-62003 final-binary
replay has no floating mana at decisions 25 and 28. Both focused tests pass,
including a subsequent Time Warp cast with the 6/6 land remaining untapped.
The full unit suite passed 2,664 cases and 3,578,487 assertions.

**Viewer validation flag resolved:** seed 62008, decision 13, the hand contains
no Horn of Greed; the next Horn is drawn only by the proposed Island play.
`--validate-line 'cast=Horn of Greed;landafter=Snow-Covered Island'` returns
`accept`, plan 7, whose summary and structured plan play the Island first.
The `landat=all` spelling does the same; `landat=1` accepted plan 8 directly.
The proposed land-after plan 8 is
not payable as declared: direct replay plays the Island, draws Horn, and drops
the attempted pre-land Horn cast. `CheckLine`'s positioned-line fallback
intentionally matches a legal non-enumerated line to a land-first plan. Its
single-hand-cast guard now rejects this match when the cast card is absent
from the pre-land hand and the matched plan has no earlier action that could
draw it. `landafter=`, `landat=all`, and `landat=1` now return `illegal` with
failed action `cast=Horn of Greed`, while the legal land-first and `landat=0`
lines still return `accept` for plan 7. A focused unit test also accepts the land-after line
when Horn is already in hand. No illegal state transition was observed.

**Engine plan and audit flag resolved:** the old menu offered plan 8 as
`land=Snow-Covered Island (after Horn of Greed); cast: Horn of Greed`, with one
`CastFromHand` action. Before the decision, one Horn is on the battlefield,
none is in hand, and the library's top card is Horn. Selecting plan 8 from
that state plays the Island and draws the Horn, then reaches decision 14 with
one Horn on the battlefield and one in hand: the declared pre-land cast did
not execute. Selecting land-first plan 7 instead reaches decision 14 with two
Horns on the battlefield and none in hand. Yet the old `--validate-line`
audit marked plan 8 `engine_full: true`, whose documented meaning is that every
action executed. The follow-up fix removes single-cast pre-land proposals when
the card is absent from the starting hand and reports missing-hand casts to
the dropped-cast sink. On the rebuilt binary, seed 62008 decision 13 still
offers the legal land-first Horn plan at index 7, and no Horn-before-land plan;
the focused test verifies a stale positioned Horn plan has
`engine_full: false` with Horn in `dropped`. No illegal state transition was
observed.

The old-source 5,000-game mana-clean arm was stopped after 30 completed
on-arm jobs and about 155,000 capped-decision lines. Its exit 143 and logs
(`/tmp/mana-extra-turns-resume8-20261110.log`, `on.2.{out,err}`) are diagnostic,
not accepted clean-gate evidence. The reference safety scan passed across
191 checkout/worktree sets; no ground truth was accepted or rebaselined here.

### Final verification (2026-11-07)

On the final binary, all 2,638 unit cases (3,577,665 assertions) and all
445 scenarios pass. `verify_deck.py` passes coverage of all 15 cards, the
712-card field snapshot, play-viewer surface and wiring, both one-game
nonconvergence/full-depth mismatch arms at seed 60001, and the 174-decision
mechanical replay. The viewer audit samples up to 160 decisions per game;
extra-turn games that reach the cap while their `turn_seq` is still advancing
are productive samples, while a stalled decision sequence remains a failure.
This repaired a false stuck-loop result on 21 of 40 games. The independent
559-card Scryfall cost audit passed earlier; this final verifier run used
`--no-network`.

The mechanical replay also prints 240 advisory-only static availability notes
for offered plans that name a spell absent from the hand at the start of the
plan. The checker does not model cards drawn earlier within that same plan;
the confirmed Horn-into-Time-Warp line is one such case. These notes were not
individually adjudicated, so they remain a review limit rather than a passed
legality assertion. The replay's determinism, state-integrity, and progress
checks have no hard flags.

The verifier still fails overall because this deck is outside all three
regression tiers and has no accepted ground truth. The measured search-cost
lower bound above prevents suite admission under the 3× rule; it also blocks
the value leaf and exhaustive mulligan profile. The final-binary Claude-play
gate is disclosed as skipped because Claude CLI exhausted its weekly limit.
The reference safety scan passed across 24 checkouts/worktrees, and
`git diff --check` found no whitespace errors.

## Extra-turn chain heuristic experiment (2026-11-07)

Extra turns continue to count as physical turns for safety and per-turn resets,
but not as scored rounds. The search already restores a ply when it enters an
extra turn, following the Emrakul-in-Cyclone precedent. This experiment adds an
optional same-round chain trial (`MTG_ET_CHAIN_WALK=1`): depth-zero play chooses
legal whole-turn plans, keeps at least one extra turn queued at each handoff,
and returns a candidate only if it reaches a resolved win. The trial records
the actual sequence. Each later real extra turn may replay its next plan only
when the physical card order, hand, battlefield, and mana-relevant turn state
match the trial; otherwise it falls back to full search. It never labels the
trial as a verified win before executor replay.

The optional Extra Turns card-value policy (`MTG_ET_CHAIN_VALUE=1`) favors two
draw engines, preferring Howling Mine to Horn of Greed, then Extraplanar Lens
once imprinting will not delay the first turn spell. It prioritizes turn spells
when the chain would otherwise end, saves Walk the Aeons while another turn
spell is affordable, and values buyback only when sacrificing three Islands
leaves the next turn's spell mana intact. The initial custom cleanup policy
(`MTG_ET_CHAIN_DISCARD=1`) protected the Eldrazi and turn spells before shedding
surplus lands. At this stage all three switches defaulted off. The later
cleanup buckets are described below. The chain walk and card values were
enabled by default after the later paired comparisons.

The first chain version re-decided each extra turn and lost seed 60001 after
drifting away from its winning trial. Guarded replay wins that seed in round 4
and matches all 11 continuation states. Seed 60009 exposed a separate real
rollout bug: a Howling Mine draw could empty our library, but the continuation
still entered main phase and credited a later Emrakul attack. The rollout now
stops on that draw loss, matching the executor; a focused unit case locks down
the draw-step boundary. With the fix, guarded replay wins seed 60009 in round 4
without a mismatch. Both seeds had no nonconvergence or oracle mismatch report.

Depth-zero, round-8 horizon, 1,000 games per block (unwon scored 9): the
committed baseline before this experiment was 8.528 (138 wins) on seed
20261107 and 8.492 (147 wins) on seed 20270001. With card values on and the
custom discard off, the corrected engine averaged 7.537 (388 wins) and 7.616
(375 wins). Turning custom discard on gave 7.625 (399 wins) and 7.627 (400
wins): more wins but a worse loss-penalized average. At depth 5, budget 1 ms,
round-8 horizon, eight paired seeds 60001 and 60004–60010 with custom discard
ON in both arms gave three one-round improvements, five ties, and no worse
games with the guarded trial versus card values alone. On eight fresh seeds
61001–61008 under the same controlled setup, seven completed in both arms
with identical win rounds; seed 61007 finished in round 6 with the walk but
the value-only arm timed out at 75 seconds. Its eventual outcome is unknown.
An earlier comparison let the walk switch enable custom cleanup only in one
arm; the switches were then made independent. Its three apparent timeout wins
were confounded and are not evidence of the walk's effect. A depth-3 batch
using the same mismatched arms
likewise showed a 62006 round-5-to-6 regression that was attributable to
cleanup: holding custom discard ON in both arms gives round 6 in both.
Eight controlled depth-3 pairs (seeds 62001–62008) gave one improvement
(62005, round 7 to 6) and seven equal win rounds. One of the ties is a material
runtime regression: seed 62007 wins in round 4 in both arms, but the isolated
guarded-walk run takes about 108 seconds versus about 30 seconds with values
and custom discard alone. It spent 53,312 enumeration-memo misses versus 329.
The unbounded chain trial needs a work-cost control before adoption. These
searched samples are also too small to satisfy the heuristic-adoption bar;
the optional policies remain off by default pending a larger review.

A separate 10,000-game depth-zero confirmation (seed 30300001, same round-8
horizon, original cleanup rule) averaged 8.5307 with the generic card values
and 7.5680 with the Extra Turns values, with 1,370 and 3,870 wins
respectively. This supports the card-value ordering at depth zero; searched
quality and the chain walk remain separate judgments.

A four-seed depth-3 check with original cleanup (seeds 62001, 62004–62006,
game index 0, 1-ms budget) found a real value-ordering regression at seed
62005: generic values won in round 8, while the Extra Turns values did not
win by the round-8 horizon. Seed 62004 tied at round 8; on 62001 and 62006
the generic-value arm timed out at 60 seconds while the Extra Turns arm won in
round 5. The wider batch's `gi=4` round-7 result for derived seed 62005 is a
different game-index stream: reproducing it with `--game-index 4` wins in
round 7, so it does not erase the index-0 regression. This searched result
blocks adopting the value heuristic despite its large depth-zero gain.

The seed-62005 logs show why the switches interact: the value-only line chains
through 26 extra turns in round 7 but repeatedly discards both Eldrazi at
cleanup, then ends unwon. With custom cleanup enabled it wins in round 7.
The same seed at game index 0 and depth 3 scores generic values/original
cleanup 8, Extra Turns values/original cleanup 9, generic values/custom
cleanup 9, and Extra Turns values/custom cleanup 7. The joint policy needs
its own broader searched validation; neither component is safe to adopt
from its isolated depth-zero result.

At depth 5 with a 20-ms budget, seed 60001 still wins in round 4 with all
three experiment switches on (one trial win, 11 matched continuation states),
versus round 5 on the default verifier arm. The experimental run took about
140 seconds and the default batch about 193 seconds. Seed 60009 finished in
round 5 at the same setting; its chain trial did not find a win, so the earlier
round-4 result at a 1-ms budget does not generalize to this budget. This is
another reason to keep the walk experimental.

After the draw-loss repair, the Release build passed 2,639 unit cases
(3,577,671 assertions) and all 445 scenarios. The reference safety scan passed
24 checkout/worktree sets; `git diff --check` passed. The existing suite-tier,
ground-truth, and final Claude-play gates remain pending as disclosed above.

## Follow-up: missing finisher and Lens lines (2026-11-07)

The 7.5 depth-zero average hid two concrete lost lines. A 200-game trace showed
games that chained many extra turns, drew one or both Eldrazi, and then spent
their mana on more turn spells or draw artifacts until drawing the library
empty. Generic creature value reaches zero at the scored-round horizon even
when physical extra turns remain. `ExtraTurnsProvider::ComboCardValue` now
gives the finishers a deck-specific value under the existing experimental
`MTG_ET_CHAIN_VALUE` switch. Ulamog is held behind an affordable turn spell
when the extra-turn queue is empty; Emrakul's cast itself queues a turn. In a
paired 200-game depth-zero block (seed 20261107, custom cleanup on), the
average moved 7.485 to 6.790; 61 games improved and two worsened. On 10,000
games (seed 30300001), the same policy moved 7.5828 to 6.8818.

The same trace showed Extraplanar Lenses cast without imprint. Lens's decline
and physical-land target actions had equal immediate value, so the depth-zero
chooser often selected the first, empty Lens even with enough Snow Islands for
a productive imprint. A provider-owned `LensImprintValueBonus` now orders the
imprint variants while keeping every legal option available to searched play
and the viewer. Four Snow Islands before Lens are sufficient: imprinting one
leaves three doubled Islands (six mana) next turn. With finisher value and
custom cleanup on, 10,000 depth-zero games averaged 6.2434 on seed 30300001
and 6.2552 on held seed 40400001. Disabling only finisher value on the held
block gave 6.9413. Thus the large gain is reproducible, but depth-zero is a
heuristic diagnostic, not the shipped searched average.
The actual default-off arm on the same held 10,000 games averaged 8.5316;
this is why the user's concern about the earlier 7.5 figure was warranted.
The held block listed 1,503 unwon games with the corrected heuristic and
8,623 with the default heuristic (max-turn cap eight, unwon scored as nine),
so the improvement is primarily more completed chains, not a changed score
for already winning games.

The user's later finisher timing rule is now explicit in the experimental
walk: if Emrakul is already castable, the depth-zero trial rejects a plan
without it; Ulamog requires either a queued extra turn or a turn spell cast
in the same plan (Emrakul's cast also qualifies because it grants its own
turn). Emrakul's plan value was raised to make land/Lens-enabled casts win
the depth-zero ordering, and Ulamog's value is negative when neither a
queued turn nor a jointly affordable turn spell is available. A focused unit
case covers the trial guard. The previous numeric A/B results predate this
last finisher-timing change. On the same held 10,000-game depth-zero block
(seed 40400001, eight-round cap), the revised policy averaged 6.2432 with
1,496 unwon, compared with 6.2552 and 1,503 unwon before the timing rule.
The exact default-off arm remains 8.5316 with 8,623 unwon.
The rebuilt binary passed 2,641 unit cases (3,577,703 assertions) and all
445 scenarios. The eight-game depth-3, 1-ms chain replay sample (seed 62001)
still averaged 5.1250 with zero replay-key mismatches: 11 trial attempts,
five same-round trial wins, and 39 matched continuation steps. A larger
replay check is recorded separately when complete.
The paired no-shortcut arm on those same eight games averaged 5.2500, so
the experimental replay retained its one-round total gain in this block.
The 24-game depth-3, 1-ms chain arm (seed 63001) averaged 5.4167 after the
finisher timing rule, with 93 attempts, 13 trial wins, 76 matched continuation
steps, and two replay-key mismatches. The earlier rule averaged 5.3333 on
that block with six mismatches; these are different policies, and the new
rule trades some shortcut wins for the requested finisher timing. The
remaining mismatches still keep the shortcut default off.
The paired ordinary-search arm with the same new finisher rule averaged
5.5417 on those 24 games, so the shortcut saved three total scored rounds
on this block. Its two replay mismatches fell back to ordinary search.

Tracing a searched loss found `WalkBuybackValueBonus` counted all Lenses,
including one with no imprint. It projected eight mana after sacrificing three
Islands when the real board had only four, bought back Walk twice, and stranded
the next spell. The estimate now counts only Lenses imprinted on
Snow-Covered Island. The reproduced game (seed 63018, index 17, depth 3,
budget 1 ms) moved from unwon by round 8 to a round-7 win.

The chain trial exposed two rollout/executor parity defects. The noncreature
`ApplyPlanDirect` path dropped Lens's chosen physical imprint target, so its
rollout kept an empty Lens while the real executor exiled an Island. A focused
unit case now checks that applying the searched Lens plan exiles the chosen
Island and projects six mana after untap. Next, a depth-zero chain plan left
the land drop undecided: the rollout played it greedily, but the searched
executor replayed the spell without that land. The trial now freezes the
chosen land name into each tape step. A hypothetical evaluator call could also
arm the global tape; `FullSearchLineHybrid` now accepts a real-executor gate,
set only at the committed call site. These changes preserve the scored-round
rule: extra turns untap/draw/play lands and advance `TurnSeq`, while
`turn_number` stays on the same scored round.

After these repairs, eight paired depth-3, 1-ms games (seed 62001, indices
0–7) averaged 5.2500 without the chain shortcut and 5.1250 with it: one
round earlier, seven tied. The chain arm made 11 trials, found six same-round
wins, replayed 43 continuation steps, and had zero state-key mismatches.
On a separate 24-game depth-3 block (seed 63001), the corrected ordinary
searched arm won all 24 by round 8 and averaged 5.6667. The earlier 24-game
figures measured before the Lens rollout repair are superseded. The chain
shortcut averaged 5.3333 on the same 24 games, but six of its 23 winning
trials had a later replay-key mismatch and fell back to ordinary search.
The 8-game clean replay result therefore does not generalize to all chained
states. The shortcut and card-value/cleanup policies remain experimental
pending a larger searched and cost gate; the result above is evidence of
missing lines and their repair, not an adoption claim.

The final default-off comparison on seed 63001 stopped after 22 of 24 games:
two default games consumed more than 18 minutes without finishing, so this is
explicitly a **partial paired sample**. On the same 22 indices, default-off
averaged 8.1818 and the corrected value-plus-cleanup policy averaged 5.7273;
18 improved, two tied, and two were slower by one round (indices 11 and 23).
Those slower searched cases block default adoption under the repository's
depth-8 unlimited recovery rule, even though the net sample strongly favors
the new policy. The final two default games have no measured outcome.
Index 11 is a concrete early-land ordering example: default plays Myriad
Landscape on round 1 and activates it on round 3, reaching Part the Waterveil
in round 4 and winning in round 6. The experimental policy plays Snow-Covered
Island first, delays Landscape's activation to round 5, and wins in round 7.
This is a searched budget/order issue at the opening land choice, separate
from the repaired Lens and extra-turn mechanics. No depth-8 unlimited recovery
has yet been measured for indices 11 or 23.

The corrected Release binary passed 2,640 unit cases (3,577,681 assertions)
and all 445 scenarios. The final mismatch and suite-tier checks are recorded
separately when run; the Claude-play gate remains unavailable under the
current Claude CLI quota.
The final offline coverage check reports all 15 cards full with none missing;
the static viewer audit finds all five expected decision types and no unmodeled
oracle-text choice phrase.

The final binary's full `verify_deck.py` refresh completed the 40-game viewer
decision sweep, then hit its 10-minute outer cap during the depth-3,
20-ms mismatch batch before it could write a new generated gate table. Those
searched arms were then run directly: seed 60001 at depths 3 and 5 with the
verifier's 20-ms budget each won in round 5, produced the same
`2159cc7c96125238` digest, and emitted no nonconvergence or oracle-mismatch
flag. The mechanical play replay again passed all hard invariants across 174
decisions; it emitted 241 advisory static-availability notes that do not model
cards drawn earlier inside an offered plan. The generated gate table above is
the earlier verification record, not a claim that this timed-out refresh
completed end to end.

### Final regression and replay status for this follow-up

The final corrected binary passed the 2,640-case unit suite (3,577,681
assertions), all 445 scenarios, `git diff --check`, and the reference safety
scan of 24 checkout/worktree sets. The regression smoke suite finished with
176 passing configurations and three failures outside Extra Turns:
`cyclone_smoke_d5_s1001` and `landfall_smoke_d5_s1001` kept their scores but
changed play digests; `landfall_smoke_d0_s1001` moved 7.4400 to 7.4430.
The searched per-game audit found no slower games, two changed lines at the
same score. The depth-zero audit found three slower games. These failures are
not rebaselined or counted as a clean regression pass. Their attribution to
the pre-existing dirty worktree has not been proven.

A second full `verify_deck.py` refresh with two seeds and eight games per seed
completed its 40-game viewer sweep, then spent more than 30 minutes in the
depth-3, 20-ms mismatch batch. That run was stopped without a generated
verdict. Its final mismatch and depth-5 oracle verdicts remain pending; the
earlier one-seed direct checks above are the available
partial evidence. The Claude-play gate is also pending because the Claude CLI
reported its weekly quota exhausted, with reset on November 9. Neither gate
has a substituted pass result.
A one-game verifier rerun was interrupted in its viewer sweep when the later
finisher-timing rule changed the binary; it also produced no gate verdict.

The deck is not in any regression tier. The suite's pinned 3x cost limit for
new decks is 39,617.8 ms/game (Hinata yardstick 13,205.933 ms/game). Even the
lighter depth-3, 1-ms Extra Turns sample included a 737,401-ms default game
(seed 63024, index 23), over 18 times that limit. The 16-game depth-3,
20-ms verifier batch was still running after 30 minutes. Adding provisional
rows to all three tiers would exceed the suite budget; partial tier membership
is forbidden. The `regression_tiers` and `suite` gates therefore remain open
pending performance work and the required user sign-off for a cost deferral.
The final binary's one-game, one-round provider probe reported
`provider=ExtraTurns` under `--ignore-play-profile`; this verifies the deck's
route without waiting for a full game. The fleet-wide `provider_audit.py
--check` run was stopped after more than 15 minutes in its 47-job batch and
has no global verdict.

### Viewer and chain follow-up, November 7

The primary-checkout play viewer is running at `http://localhost:8080` and
lists Extra Turns. A user report exposed a concrete viewer gap: the engine
offered Mind Stone's `{1}, {T}, sacrifice: draw a card` as a human-play DigDraw
plan, but the page's clickable sacrifice badge was restricted to lands. The
badge now follows the offered dig plan for any untapped permanent. A focused
human-play unit case confirms the Mind Stone plan is offered.

The user also found Expropriate's dialog confusing. Its implementation stores
the caster's Time/Money vote in `chosen_x` and the opponent Money vote's
permanent in `enchant_target`; the generic picker misnamed these as `X` and an
Aura attachment. `CheckLine` now emits separate vote and take dimensions,
including both vote outcomes and distinct same-named opponent permanents. The
page explains that the passive opponent votes Money and that choosing its
permanent does not target. A focused human-play unit case confirms four
variants for two opponent permanents and no `x` or `enchant` sub; the viewer
linebuild check confirms vote first, take second. Standard viewer client and
linebuild checks passed; the live viewer served the new page. The ordinary
unit suite passed before the final Mind Stone unit case was added; the two
targeted human-play cases passed afterward (35 assertions).

The heuristic chain trial's two 24-game replay mismatches came from a reactive
draw after the recorded plan, which the committed executor did not replay.
The trial now sets its plan's dig choice to no dig before applying and recording
it. On the same seed 63001, depth 3 / budget 1 ms / 24 games, the chain arm
still averaged 5.4167, with 11 verified winning trials, 73 continuation steps,
and **zero** replay-key mismatches (previously 13 winning trials, 76 steps,
two mismatches, same average). This narrows the shortcut to replayable lines;
it does not settle adoption, the two slower searched cases, or the outstanding
full verification and suite cost gates.

The user's saved rejection `logs/play/rejections/Extra_Turns_cod_s4_gi3_t5.json`
exposed a separate viewer plan gap. With eleven Snow-Covered Islands after the
land drop and two Mind Stones, Lens can be paid by one Island and the stones;
imprinting that tapped Island leaves ten untapped Islands that each make two
blue mana. The rejected line spends those 20 mana on Ulamog and two Temporal
Manipulations. `CheckLine` had not applied Lens's imprint in its real payment
walk, and the enumerator's scalar odometer bound stopped the four-cast subset
at the board's original 13 mana before its Lens credit could price it. Both
paths now account for the same-turn gain in human and autonomous play. The exact saved
`--validate-line` replay now returns `accept` (plan 82; 2,236 plans), versus
`illegal` before the fix. Replaying that plan with the user's exact cast-order
pin, an Island imprint, and Ulamog's target answers leaves Lens with
`imprinted_land: Snow-Covered Island` and all three payoff spells out of hand.
A focused unit case checks that the autonomous search enumerates and executes
all four casts with Lens imprinted, and that human-play validation accepts the
same line. Extra Turns now ranks Lens ahead of the payoff in a selected cast
set, and whole-turn prepayment declines on a Lens-imprint line so lands retain
their increased yield when the later spells pay.

Lens's physical imprint ID was also leaking into the queue as a generic `X`
picker, immediately followed by the actual board-click imprint prompt. That
internal queue dimension is now hidden, so the choice is made once at the
resolution prompt. The viewer battlefield JSON exposes the Lens's imprinted
land name, and the page draws the exiled card tucked under that Lens like an
attachment, labeled as exiled. This is a display relationship; the land stays
in the exile zone under the engine's rules. The focused unit confirms a Lens
cast has no generic `x` sub-decision.

The subsequent saved rejection `logs/play/rejections/Extra_Turns_cod_s9_gi8_t5.json`
is correctly rejected: its board has nine Islands and a Myriad Landscape, with
no Mind Stones. Paying three for Lens with Landscape and two Islands, then
imprinting a tapped Island, leaves seven untapped Islands for 14 mana. Emrakul
costs 15, and a second Myriad Landscape enters tapped as the land drop. The
user confirmed this line was one mana short.

The final Release build passed 2,644 unit cases (3,577,725 assertions), all
13 Extra Turns cases in both ordinary and human-play modes, the viewer client
and linebuild checks, and the reference safety scan. The viewer server stayed
running at `http://localhost:8080`; an already-open game retains its pinned
older engine until a new game is started.

One additional searched-game comparison completed after the chain replay fix:
seed 63024 / index 23 at depth 3 and 1 ms won in round 5 with the chain
switches off, versus round 6 on the previously measured chain arm. The
The shortcut was not adopted at that earlier stage; later paired reference
and held-block comparisons, reported below, led to its current default-on
trial. This slower case remains a recovery check for future tuning.

## Ten human references and cleanup buckets (2026-11-07)

The ten saved play-viewer references (`references/Extra_Turns/claude_s1_gi0.json`
through `claude_s10_gi9.json`) won in scored rounds 4, 6, 4, 5, 5, 5, 4, 5,
5, and 5: mean 4.8. Extra turns are tracked separately from scored rounds;
one round-4 win continued through `turn_seq` 18. Strict viewer replay found
zero play drift, board divergence, enum gaps, or contract failures; the latest
replay repaired decision indices in all ten references.
The shipped-depth search before the new cleanup buckets averaged 6.9 on
these same hands (unwon by round eight scores nine); three games did not win
by the horizon.

The reference traces exposed a concrete cleanup loss: the generic discard
order repeatedly pitched Emrakul or Ulamog during an active extra-turn chain
while retaining a surplus land or draw artifact. A second searched trace had
only eight extra-turn spells and Emrakul in a nine-card hand and pitched
Emrakul to reach seven. `ExtraTurnsProvider` now uses
the deck's cleanup bucket order by default (`MTG_ET_CHAIN_DISCARD=0` restores
the former generic order). It keeps turn spells ahead of every ordinary card,
keeps both Eldrazi finishers, and pitches a fourth draw engine once three are
in play with at least one Howling Mine. The engine quota prefers Mine to Horn,
and retains a Mine as an upgrade to a board of three Horns. It protects one
buyback Walk and the cheapest two other turn spells, then discards surplus
turn spells before either Eldrazi when the hand contains nothing less useful.
When a finisher is in hand and three Snow-Covered Islands are in play, the
first productive Lens is also kept ahead of surplus turn spells; the first
two draw engines retain priority over that Lens. Only one Lens is retained:
once one is on the battlefield, every hand copy is disposable at cleanup. The
walk can still cast a second Lens earlier in the turn if the mana available
after paying its cost and imprinting an Island at least matches the mana
available before casting it; a third receives no setup credit. Excess lands
are next. The land quota protects enough
sources to approach seven mana early, including an early miracle extra turn.
Below seven projected mana it keeps at least two hand lands and enough drops
to reach seven. After seven mana it reserves one land with a Howling Mine in
play, or two without one, then may pitch the others. The quota also reserves
enough Snow-Covered Islands for blue mana; a land drop feeds an active Horn.
The focused unit fixture checks those orderings and the mana-breakpoint land
quota.

In a fixed ten-reference depth-zero comparison with the experimental chain
walk and card values enabled, the old discard order averaged 5.9 and the new
order averaged 6.0. One reference slipped from round 5 to 6: preserving
Emrakul changed the subsequent choice toward Lens, and that fallback line
failed to sustain the chain. This is a concrete remaining chain-policy issue,
not evidence that discarding Emrakul is desirable. With both experimental
chain switches off, both discard orders averaged 8.7 at depth zero. Turning
on the chain walk alone also averaged 8.7, while the card values alone
averaged 6.0; on these references the card values account for the observed
depth-zero gain and the current chain walk added no further wins. Turning
on the existing shared idle-tapped-land ordering with the new discard buckets
lowered the final chain experiment from 6.0 to 5.7 on these ten games, but
reference 3 changed from a round-7 win to no win by round 8; that flag
remains off pending broader evidence. The references suggest that playing
Myriad Landscape on round one, then an untapped Island on round two, can
provide the early activation needed for a round-four chain. The search still
chooses Island first in some such positions, so that route needs further work.

The idle-tapped-land experiment exposed an affordability-probe bug: its first
land-drop check counted Mind Stone's sacrifice-to-draw cost while the Stone was
still in hand. That activated ability is available only after Mind Stone is on
the battlefield. The hand-only probe no longer counts it. The shared
idle-tapped-land flag remains off by default until the corrected rule is
measured on paired games and against the other decks.

On a separate held block of 2,000 depth-zero games (seed 66001, eight-round
horizon, both chain experiment switches on), the final cleanup buckets
averaged 6.1240 against 6.1910 with the generic discard order. The
chain-preservation value change, surplus-turn-spell bucket, and Lens protection
each improved this same block from the earlier 6.2880 bucket result; paired
ten-reference depth-zero results moved from 6.0 to 5.9 after those changes.
The searched-depth reference comparison is tracked separately because depth
zero rarely reaches the decisive cleanup state by itself.
An untouched second 2,000-game block (seed 68001) favored the final buckets
more strongly: 6.0175 versus 6.1880 with generic discard under identical
chain settings. Both blocks count an unwon game as round nine.
With the corrected idle-tapped-land rule enabled, the same two blocks moved
from 6.1240 to 6.0440 and from 6.0175 to 5.9720. The ten references moved
from 5.9 to 5.7 at depth zero, although reference 3 changed from a round-seven
win to no win by round eight. At full depth with the final third-engine value,
the flag worsened the ten-reference mean from 5.5 to 5.7, including a
round-eight reference 3, so it remains off by default.

The full-depth ten-reference comparison with Lens protection and the
chain-preserving turn-spell value averaged 5.5 scored rounds. Its results were
5, 7, 6, 6, 5, 5, 5, 5, 5, and 6. Reference 5 improved from round seven on the
earlier chain arm to round five: the line kept a Lens, cast it alongside a turn
spell, then cast Emrakul and Ulamog in the same scored round. The corrected
idle-tapped-land ordering averaged 5.4 on those hands, improving references 4
and 10 but slowing reference 3 from round six to seven. It remains off by
default. A third-engine card-value refinement improved both 2,000-game held
depth-zero samples (6.1240 to 6.0015; 6.0175 to 5.9325). At full depth it
matched the earlier policy's 5.5 mean on the ten references, so the gain on
held blocks supports keeping it. Turning the chain trial off while keeping
these card values also scored 5.5 on the ten references; the trial is a
verified shortcut whose runtime and hit rate still warrant measurement.

The final Release build passed 2,646 unit cases (3,577,873 assertions). A
strict replay of all ten new references again had zero play drift, board
divergence, enum gaps, and contract failures; all ten needed stale-index
repair only. The reference safety scanner confirmed all saved
games are protected on origin or the autosave branch. The viewer still serves
at `http://localhost:8080`.

## Overnight reference and cost pass (2026-11-07)

The first heuristic-walk result in this analysis was an experiment, not the
shipped search result. The initial depth-zero 10,000-game block averaged
6.2432, and a 24-game searched sample averaged 5.4167. More recent policy
work changed opening land sequencing, cleanup discards, Lens timing, chain
ordering, and the chain trial's work budget. These later measurements supersede
the initial estimate.

The chain trial now has a deterministic priced-work ceiling across the entire
trial and a 64-plan value-ranked prefix at each depth-zero continuation. A
successful trial still replays through the real executor and verifies its state;
if it misses or exceeds its ceiling, ordinary search receives the decision.
An Extra Turns-specific per-decision priced-work allowance limits repeated
searched decisions inside a single scored round. No extra turn increments the
round-to-win metric.

Two further order rules were measured. Below seven currently available mana,
the search's plan list keeps legal land-drop plans when any exist; this prevents a short
horizon from skipping early land development (reference 2 had skipped turn one,
then turn two when turn one was forced). In the depth-zero walk, Temporal Mastery
is preferred over a simultaneously held Nexus of Fate so the recyclable Nexus
remains for later draws. Both rules have opt-out environment flags for A/B.

At depth 5/budget 20, the ten reference games improved from a 5.3 searched
average to 4.9 under the final candidate: reference 2 wins in round 5 versus
the recorded round 6; reference 3 reaches the recorded round 4. References 6
and 7 remain one round slower than their saved lines. On two disjoint 100-game
searched blocks, the paired average improved from 5.255 to 5.200. The final
candidate's block costs were 10,938 and 8,687 core-ms/game with no other batch
work running. The default depth-5/budget-20 setting remains the play policy;
putting an enabled value_play block in the profile did not steer it, because the
profile loader deliberately reads only mulligan-generation keys from that block.

The Extra Turns bucket override gives 14 classes from 15 distinct card names.
Only Temporal Manipulation and Time Warp merge. The profile declares the
expected count of 14; its mulligan-generation depth and budget are left to the
required setting screen in `mullgen.sh run`.

The final Release binary passed 2,646 unit cases and 3,577,873 assertions.
Strict replay reproduced all ten Extra Turns references, and the regression
run's full replay covered 787 references with zero play drift, board divergence,
enum gaps, or contract failures. The same-run cost gate measured Extra Turns at
12,797.770 core-ms/game and Hinata at 4,293.987, a 2.98x ratio under the pinned
3x limit. The deck entered all three regression tiers. Its smoke depth-5 row
averaged 5.0200 over 100 games; regression depth-5 rows averaged 5.0667 and
5.1000 over 150 games each; overnight depth-5 rows averaged 5.0900, 5.0533,
5.0500, and 5.2500 over 300 games each. All new rows passed their audits and
were accepted as ground truth.

## Current artifact handoff (2026-11-08)

Source commit `e22ef9fb0` corrected the extra-turn walk's Emrakul cost read,
added a chain-preserving legal-plan fallback including explicit Mind Stone
sacrifice plans, and placed the narrow turn-three Myriad preference in the
Extra Turns provider. The updated ten-reference benchmark is 4.7 searched
rounds versus 4.8 recorded, with no late references. Strict replay reproduced
all ten; the Release unit suite passed 2,646 cases and 3,577,873 assertions.
The same-run suite cost gate passed at 2.63x Hinata. Benchmark and cost artifacts
were committed in `255731460` and `a7bf5e7ad`.

The `fast` mulligan generator is running as PID 610346, with `mtg-analyze` PID
620829, at source tree `bd3d785377400fad0c2d1329f20a62e4c9ca9110` on
PGO+LTO. Its journal is under `decks/Extra Turns/`; progress is readable with
`python3 scripts/mullgen_progress.py 'decks/Extra Turns'`, and its report is
`logs/Extra_Turns_mullgen/VALIDATION.txt`. At the last check it had finalized
the 86,150 size-seven keep cell sides and was refining the 52,616 bottoming
cell sides. The journal and rolling backups from the superseded play version
are archived in `logs/et_superseded_mullgen_e22ef9fb0/`.

The value leaf is queued by `logs/et_valueleaf_queue.sh` (PID 649949). It waits
for the mulligan driver to exit, verifies `VALIDATION PASSED`, a live keep
profile, and the source-tree fingerprint, then runs
`bash scripts/valueleaf.sh run 'decks/Extra Turns'`. Its output is
`logs/et_valueleaf_queue.log`; `logs/et_valueleaf_queue.status` is the concise
state. A separate two-hour status checker runs as PID 649958 and writes
`logs/et_status_timer.log`. Both use the local Codex daemon's `queue --resume`
to send status or terminal events back to this same session. A queued test
message reached the session after the prior turn ended, confirming the idle
delivery path. The daemon is available through `codex app-server daemon`.

After mulligan validation, inspect its A/B and regression report, gzip and
commit the definitive raw sidecar and keep profile, and handle the deck's moved
ground truth. After value-leaf completion, read
`logs/vlq_extra_turns/DECISIONS.txt`, verify the adoption and play-setting
measurements, then commit the selected artifact and corresponding ground truth.
Do not run another generator alongside either stage. The automated handoff
does not itself commit artifacts.

## Value-leaf matrix tail investigation (2026-11-09)

The matrix is paused at the user's request; phase A rows (23,954 pooled), the
9,124-row train/holdout split, phase B model (heldout RMSE 0.4474), phase C0
calibration, and 105 matrix attempts remain banked. There is no active
generator. At the measured rate the remaining matrix projected to about
2,489 core-hours, so resuming it unchanged would take roughly 104 hours on
24 cores. Of 170 unique observed attempts in the expensive-first sample, 36
exceeded 30 seconds and 29 hit the two-hour predictive cutoff. This is a tail
problem, not a worker-count problem; the sample is scheduled expensive-first,
so its slow percentage is not a population estimate.

The reproducible H2 game is seed 10010, game index 0, max round 8, unbounded
budget. A Release run with the experimental candidate prunes still exceeded
two minutes; end-of-turn dominance exceeded 90 seconds. A Profile stack sample
was in ordinary depth-one rollout search, expanding 1,152 plans at a late
extra turn. It was not stuck in the chain trial or a memory cap. The same seed
at depth zero finishes in 2 ms and wins in round 5, chaining multiple spells
to Ulamog and Emrakul. Its real line casts Nexus of Fate and Time Warp in one
main phase; therefore a blanket one-turn-spell plan prune deletes a useful
legal line and is not an acceptable search optimization.

Experiments are saved in `logs/et_tail_experiments.patch`; all source changes
from these experiments were restored after measurement. The paired 200-game
depth-5/budget-20 screen (seed 88001) found:

| arm | mean round | summed CPU ms | units | earlier/later vs baseline |
|---|---:|---:|---:|---:|
| baseline | 5.170 | 425,566 | 1,763,956 | — |
| opponent-turn target + one-turn-spell prune | 5.145 | 223,132 | 1,714,617 | 7 / 2 |
| rollout land before spells | 5.125 | 531,606 | 2,158,138 | 30 / 18 |
| shared rollout land ranker | 5.175 | 456,075 | 1,932,871 | 4 / 4 |
| land before spells + shared ranker | 5.125 | 538,049 | 2,079,700 | 30 / 18 |

The ordinary depth-zero rollout chose spells before its heuristic land drop,
and the legacy rollout land picker chose a tapped Myriad Landscape over an
untapped Snow-Covered Island. In the H1 seed-10010 trace that delayed Howling
Mine from round 2 to round 3 and the greedy continuation's win from round 5
to round 6. Enabling land-before-spells with the shared ranker reproduced the
round-2 Mine and the round-5 rollout win. An experimental verified depth-zero
incumbent then bounded full search to strictly earlier rounds, but the complete
H1 game still exceeded 60 seconds. The extra-turn same-round search needs a
stronger, coverage-preserving state or sequence dominance argument before the
matrix should resume. Any adopted rollout change must recheck the frozen play
digest before using banked phase A labels.

### Rollout correction and tail retest

The late extra-turn leaf was still receiving a one-ply search after the
ordinary depth-zero rollout crossed into an extra turn. Routing that leaf to
the existing chain heuristic removed the nested plan explosion. Two supporting
changes matter: play the Extra Turns rollout land before choosing spells, using
the shared untapped-land ranker; and use a verified depth-zero win as the
unbounded search's incumbent. The chain trial can resolve a win in the current
scored round; that result is optimal at its node. Extra turns still do not
advance the scored round.

With all five Extra Turns switches plus the shared ranker on, four seed blocks
at H2/H4/H5 (240 games) had **zero games over 30 seconds**, with a 5.4-second
maximum. V8 across the same blocks (80 games) had zero over 30 seconds and an
11.4-second maximum. On a same-binary paired 200-game depth-5/budget-20 block,
the baseline averaged 5.170 (414,461 summed CPU ms) and the candidate 5.025
(318,112 ms): 41 games earlier, 14 later, 145 unchanged. The 10 hand-played
references averaged 4.8; the candidate averaged 4.6, with no shortfalls.

The original value-leaf queue is frozen at `48267e612` and banked phase A/B
labels belong to that source. The value-leaf driver checks the changed play
digest on resume and retains only complete, consistent matrix chunks across
the source transition; it regenerates incomplete sets. Resume the existing
queue after the clean-environment verification and source adoption, and
inspect its digest transition report before accepting the matrix.

The optimized Extra Turns rollout is now default-on with individual `=0`
legacy switches. The clean-environment 200-game digest matches the experimental
arm (`d78304ec84f7db44`); disabling all six Extra Turns switches reproduces
the prior digest (`e3ee8ec1228348c2`). The strict viewer protocol replay
reproduced all 10 Extra Turns references with no play drift or enumeration gap.
All 445 rule scenarios passed in smoke, regression, and overnight tiers.
Ground truth was accepted after per-game audit. In the overnight tier every
Extra Turns case mean improved; among 241 searched games slower than stored
ground truth, 74 recovered at larger budget, 125 followed divergent draws,
and a same-binary legacy comparison of the remaining 42 found 38 ties, three
improvements, and one actual one-turn slowdown.
