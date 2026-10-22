#pragma once
#include "Card.h"
#include "Library.h"
#include <vector>

// A card exiled by Light Up the Stage (or similar) that can be played until expiry_turn.
struct StagedCard
{
    Card card;
    int  expiry_turn = 0; // last turn on which this card may be played
};

// A card exiled with time counters by SUSPEND (Lotus Bloom: "Suspend 3-{0}"). Unlike a StagedCard
// it is NOT playable while it waits; at each of the controller's upkeeps a time counter is removed,
// and when the last is removed the card is CAST off suspend (arrive_turn = suspend_turn + counters).
// See CardParams::suspend_time_counters, GameEngine::UpkeepStep / SimulateEndAndStartNextTurn (the
// arrival), and CastOffSuspend (the shared free-cast site).
struct SuspendedCard
{
    Card card;
    int  arrive_turn = 0; // the upkeep turn on which the last time counter is removed -> cast off suspend
};

struct Player
{
    int life                       = 20;
    // DESCEND, The Mycotyrant: "X is the number of times you descended this turn", where you
    // descend "each time a permanent CARD is put into your graveyard from anywhere". This is the
    // graveyard's SIZE at the start of this player's turn, refreshed at BOTH untap sites in
    // lockstep with cards_drawn_this_turn below; DescendedThisTurn() (SpellEffects.h) counts the
    // permanent cards in graveyard[this .. end).
    //
    // IT SITS BESIDE AN UNRELATED FIELD FOR A LAYOUT REASON, and the reason is measured. The four
    // bytes at offset 4 were padding before `hand`'s 8-byte alignment, so here the int is free:
    // sizeof(Player) stays 200 and sizeof(GameState) stays 1080. Declared beside the graveyard or
    // the per-turn counters -- its natural homes -- it instead lands after a full 8-byte slot and
    // costs 8 on Player, hence 16 on GameState, which the search deep-copies at EVERY NODE on the
    // heaviest deck in the suite. Both layouts were compiled and printed (208/1096 there,
    // 200/1080 here), not reasoned about. Same trick, same justification, as
    // GameState::deck_reads_lifegain_in_play.
    //
    // WHY A SNAPSHOT AND NOT AN EVENT COUNTER. The faithful model is a counter incremented wherever
    // a permanent card reaches the graveyard -- and there are 76 `graveyard.push_back` sites in
    // src/ with no chokepoint (EffectHandler::MoveToGraveyard covers only the stack->graveyard trip
    // of a resolved spell, not deaths, sacrifices, depletion lands or a faded Saproling Burst).
    // Instrumenting all 76 is a wide change to a mature engine for one card, and its failure mode
    // is SILENT UNDER-COUNTING. Deriving the count from the DESTINATION zone is complete for every
    // source zone by construction instead, and it excludes TOKENS for free -- a token ceasing to
    // exist never reaches this vector -- which is exactly the rule the card needs and the thing
    // most likely to be modelled wrong (this deck makes ~40 Saprolings a game and sacrifices most
    // of them; not one is a descend).
    //
    // THE SIZE, NOT A PERMANENT-CARD COUNT, and that is a cost decision with a correctness
    // argument behind it. A count snapshot needs the current count too, i.e. a full graveyard walk
    // with a LookupCached per card at every read -- and BuildSimKey, one of the readers, is ~15% of
    // runtime on the one deck whose sacrifice engine makes the graveyard the fastest-growing zone
    // on the board. A size snapshot makes a read O(cards that arrived this turn), usually zero.
    // The two forms are EXACT under the same condition -- the graveyard is append-only within the
    // turn -- and degrade the same way when it is not: a card that leaves and returns in one turn
    // counts once instead of twice, and one that merely leaves lowers the count. Nothing in a
    // descend deck's pool moves a card out of a graveyard (no recursion, flashback, escape or
    // exile-from-graveyard), and the GyR_DescendCount gate means no other deck computes this at
    // all. If a recursion card ever shares a list with a descend reader, this must become real
    // event counting.
    int gy_size_at_turn_start      = 0;
    std::vector<Card> hand;
    Library           library;      // index 0 = top of library
    std::vector<Card> graveyard;
    int lands_played_this_turn     = 0;
    int bonus_land_drops_this_turn = 0;  // one-time grants; reset in untap step
    // Cards DRAWN this turn (CR 121 draws only: the draw step + spell/ability draws -- NOT
    // reveal-and-put-in-hand (Treasure Hunt), staged/impulse exiles, tutors, or the opening hand).
    // Read only by Fists of Flame's pump_per_cards_drawn_power payload; incremented at the real
    // draw sites (GameEngine::DrawStep, the rollout's start-of-turn draw, both worlds' draw-spell /
    // cast_draw / dig-draw resolvers, the shared trick draw). Reset at BOTH untap sites in
    // lockstep; folded into the sim key. 0 forever for decks with no drawn-count payload.
    int cards_drawn_this_turn = 0;
    // LIFE GAINED this turn by this player (the sum of the amounts gained, NOT the net life change --
    // life LOSS does not reduce it, per "the amount of life you gained this turn"). Read only by
    // Fortifying Draught's pump_per_life_gained_power payload; incremented at every controller-side
    // life-gain site (cast_lifegain, etb_lifegain, the creature-enters watchers, gy-exile lifegain,
    // charge_lifegain, lifelink combat damage) -- deliberately NOT at the OPPONENT-lifegain site,
    // which credits the other player. Reset at BOTH untap sites in lockstep with
    // cards_drawn_this_turn; folded into the sim key under the same >0 guard, so it is 0 forever --
    // and byte-identical -- for every deck with no lifegain-count payload.
    int life_gained_this_turn = 0;
    // Cards this player has CYCLED OR DISCARDED this turn. Read only by Hollow One's
    // cost_less_per_cycle_or_discard payload ("costs {2} less to cast for each card you've cycled
    // or discarded this turn"). Incremented at the three cycle sites (executor PerformDig, the
    // rollout auto-dig loop, the human-play DigDraw action) and at the real discard sites (cleanup
    // discard, cost/trigger discards). Reset at BOTH untap sites in lockstep with
    // cards_drawn_this_turn; folded into the sim key under the same >0 guard, so it is 0 forever --
    // and byte-identical -- for every deck with no cycle/discard-count payload.
    int cards_cycled_or_discarded_this_turn = 0;
    // THE CITY'S BLESSING (CR 702.131, ASCEND -- Ocelot Pride). A player DESIGNATION, not a
    // permanent state and not a counter: once gained it is held "for the rest of the game", so
    // unlike every per-turn field above it is NEVER reset at untap and NEVER lost -- dropping back
    // below ten permanents, or losing the ascend permanent entirely, does not take it away. Raised
    // only by RefreshCityBlessing, which runs wherever the permanent count can RISE (CR 702.131b
    // makes ascend a state trigger). Read only by an endstep_token_ascend_copy trigger, and folded
    // into the state keys only when true, so it is false forever -- and byte-identical -- for every
    // deck with no ascend card.
    bool has_city_blessing         = false;
    // AN ENDURING STORY (STORIED -- Kili the Resourceful). The exact structural twin of
    // has_city_blessing above and for the same reason: "if you control three or more artifacts,
    // legendaries, and/or Sagas, you have an enduring story FOR THE REST OF THE GAME" is a player
    // DESIGNATION, so it is never reset at untap and never lost when the board shrinks (Scryfall:
    // the designation is on the player and cannot be removed by any effect). Raised only by
    // RefreshStoriedLatch, run wherever the permanent count can RISE, so a momentary peak counts.
    // THE COUNT IS ONE POOL, NOT THREE: a permanent that is both an artifact and legendary counts
    // ONCE (so this deck's 2 Shadowspear are 2, not 4). Folded into the state keys only when true,
    // so it is false forever -- and byte-identical -- for every deck with no storied card.
    bool has_enduring_story        = false;
    // "...the first equip ability you activate each turn". PLAYER-scoped and per-TURN, which is why
    // it lives here rather than on the storied permanent: an equip activated BEFORE Kili ever landed
    // still consumes that turn's first, so tracking it on the permanent would wrongly leave the
    // waiver live. Reset at BOTH untap sites (executor and rollout) or the two worlds diverge.
    bool equip_activated_this_turn = false;
    // "If you had a land ENTER the battlefield under your control this turn" (Rest for the Weary's
    // landfall CONDITION; Searing Blaze's too). An EVENT-HISTORY bit, set wherever a land enters under
    // this player's control -- the landfall chokepoint OnLandEntered (every played land, and every
    // land a generic put path or a blink routes through the enter cascade) and EnterLand (every land
    // an effect PUTS, including Genesis Wave's, whose landfall opt-out must not hide the entry from
    // this bit). It exists for the ONE event the board cannot show afterwards: a land that entered
    // and is GONE again the same turn with no land drop used -- a Genesis Wave'd Blighted Steppe
    // sacrificed for its life, a put Selesnya Sanctuary that had to return itself. Scryfall's Rest for
    // the Weary ruling 3 is explicit that this still counts ("It doesn't matter if a land that entered
    // ... is still on the battlefield, is still under your control, or is still a land").
    // Read only through LandEnteredUnderControlThisTurn (SpellEffects.h), which ORs it with the two
    // facts the engine already tracked, so it can only ever turn a FALSE into a TRUE in that one
    // vanished-land case. Reset at BOTH untap sites (executor and rollout) or the two worlds diverge.
    // DECLARED HERE FOR LAYOUT, measured: it sits in the byte of padding after the three bools above
    // and before emblem_team_pump's 4-byte alignment, so sizeof(Player) stays 200 (Dominance.h's
    // tripwire). Folded into BuildSimKey only for the vanished case and only while a hand card reads
    // it, so every existing deck keeps its exact prior key.
    bool land_entered_this_turn    = false;
    // EMBLEMS granting "Creatures you control get +1/+1" (CR 114; Gideon, Ally of Zendikar -4).
    // Held as a COUNT of stacked emblems rather than a bool: two Gideons each ultimating give
    // +2/+2, and CR 114.3 makes an emblem a permanent-for-the-rest-of-the-game object with no
    // legend rule and no way for either player to remove it.
    //
    // A PLAYER resource, not a permanent, and that is the whole reason it lives here: an emblem is
    // NOT on the battlefield, so it cannot be modelled as an anthem permanent (a fake battlefield
    // entry would be miscounted by every "permanents you control" reader and would render in the
    // viewer as a card that does not exist). It follows has_city_blessing above exactly: gained
    // once, NEVER reset at untap and NEVER lost -- unlike Benalish Marshal, whose anthem dies with
    // the body, and unlike temp_power_bonus, which cleanup clears.
    //
    // Read in ONE place -- ComputeLordBonus -- which every combat, eval and SBA path already routes
    // through (29 call sites, both worlds), so the two worlds stay in lockstep by construction
    // instead of by an audit of the readers. Folded into the state keys only when nonzero, so it is
    // 0 forever -- and byte-identical -- for every deck with no emblem source.
    int emblem_team_pump           = 0;
    int poison_counters            = 0;
    // Rad counters (Mariposa Military Base: "You may have this land enter tapped. If you do, you
    // get two rad counters"). A PLAYER resource, not a permanent's. At the beginning of this
    // player's precombat main phase they mill that many cards, and for each NONLAND card milled
    // they lose 1 life and remove one rad counter -- so the resource decays as it is used. Also
    // reduces the Base's own "{5}, {T}: Draw a card" by {1} each. 0 for every other deck.
    int rad_counters               = 0;
    // ENERGY counters (Aether Hub: "When this land enters, you get {E}"). A PLAYER resource like
    // rad counters -- any Hub may spend any Hub's energy, which is the correct model and matters
    // under a blink loop that untaps one Hub repeatedly. Spent one per COLOURED tap of an
    // energy_per_colored_tap source; NEVER reset at untap (unlike cards_drawn_this_turn) and gained
    // only when an etb_energy permanent enters. 0 for every other deck.
    int energy_counters            = 0;

    // Cards exiled by "Light Up the Stage" and similar; playable until their expiry turn.
    std::vector<StagedCard> staged_cards;

    // Cards exiled by SUSPEND (Lotus Bloom); each is cast off suspend at arrive_turn's upkeep.
    // Empty for every deck without a suspend card -> byte-identical.
    std::vector<SuspendedCard> suspended_cards;

    // OUTSIDE THE GAME (CR 400.11b): the sideboard, as a wish (Living Wish) sees it. Per-GAME state
    // rather than a static read of Decklist::sideboard, because each card is a SINGLETON consumed on
    // fetch -- four Living Wishes must see a shrinking pool, or the same one-of gets fetched four
    // times. Sits beside staged_cards / suspended_cards, the other two out-of-play zones, so it
    // rides every deep copy for free.
    //
    // Populated only for a deck that actually wishes (GoldFishRunner::DeckWishesFromSideboard), so
    // for every other deck it is an empty vector: no allocation on copy, and byte-identical.
    std::vector<Card> sideboard;

    bool HasLost() const { return life <= 0 || poison_counters >= 10; }

    // Static land-drop effects (e.g. Exploration) are evaluated by GameEngine
    // against the battlefield; one-time triggered grants increment bonus_land_drops_this_turn.
    int LandDropsAvailable() const { return 1 + bonus_land_drops_this_turn; }
};
