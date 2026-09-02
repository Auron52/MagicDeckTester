#pragma once
// THE passive opponent's library and opening hand.
//
// Before this existed, `GoldFishRunner::SetupGame` gave the opponent exactly one thing -- a life
// total. `players[1].library` was empty from turn 0 and never filled, the opponent never became the
// active player so no draw step ever ran for them, and nothing anywhere in src/ wrote to
// `players[1]` except `.life`. That silently deleted a whole class of win condition: milling or
// decking the opponent could not win and could not even be ATTEMPTED, because there was no draw for
// them to fail. See docs/design/passive-opponent-no-library.md for how that presented -- a deck
// whose intended kill was "deck the opponent" measured 2.5 turns slower than the user expected,
// with nothing in any report pointing at why.
//
// FIXED, NOT A MIRROR. The list below is the same 60 cards for every deck under test. That is a
// deliberate choice over "copy the deck under test": deck-out depth becomes a CONSTANT (53 cards
// after the opening hand), so a mill measurement is comparable across decks instead of being a
// function of the mill deck's own size. The user's spec (2026-09-02) was "it can be a fixed deck of
// some sort. Maybe have it be realish so hand manipulation like discard can be relevant, but
// otherwise it doesn't matter too much."
//
// REALISH means the type distribution is load-bearing, and it is read by real cards:
// Dimensional Infiltrator exiles the top card of their library and cares whether it is a LAND, so a
// 24/36 land split is not decoration. Discard effects need a hand to bite on, hence the 7-card
// opening hand. The opponent never CASTS any of it -- they remain fully passive -- so the cards are
// chosen only for a plausible type/land mix, and every name must exist in cards.json (a library
// card is a bare placeholder; its types come from the definition at read time, per the empty-mask
// rule in docs/design/ -- never call IsLand() on the zone card itself).
#include "GameState.h"
#include <string>
#include <utility>
#include <vector>

namespace opponentdeck {

// 60 cards: 24 lands / 20 creatures / 16 spells. Every name is a card already in cards.json.
inline const std::vector<std::pair<const char*, int>>& List()
{
    static const std::vector<std::pair<const char*, int>> kList = {
        // 24 lands -- a normal two-and-a-splash manabase's worth. The COUNT is what matters
        // (Dimensional Infiltrator's land check hits ~40% of the time, as against a real deck).
        {"Mountain",             8},
        {"Forest",               8},
        {"Island",               4},
        {"Plains",               4},
        // 20 creatures.
        {"Birds of Paradise",    4},
        {"Monastery Swiftspear", 4},
        {"Sinew Sliver",         4},
        {"Venerable Knight",     4},
        {"Goblin Guide",         4},
        // 16 noncreature spells -- a mix of instant / sorcery / artifact so a hand-reading or
        // graveyard-reading effect sees a plausible spread rather than 16 copies of one type.
        {"Lightning Bolt",       4},
        {"Swords to Plowshares", 4},
        {"Ponder",               4},
        {"Sol Ring",             4},
    };
    return kList;
}

// Cards drawn into the opponent's opening hand. Seven, like anybody else's -- they never mulligan
// (they make no decisions at all), so this is a flat draw off the top of their shuffled library.
inline constexpr int kOpeningHandSize = 7;

// Numbers assigned to the opponent's cards, offset far above any deck's own numbering so an
// opponent card can never be confused with ours in a log, a reveal, or a viewer click. Player 0's
// numbering is per-deck and dense from 1; nothing in the engine numbers past a few hundred.
inline constexpr int kNumberBase = 100000;

// Deal the library and opening hand. No-op -- and therefore byte-identical -- for every deck that
// cannot touch those zones (GameState::opponent_library_dealt).
//
// The shuffle takes a DERIVED seed through a dedicated salt, and that detail is what makes this a
// non-event for existing decks: Library::Shuffle takes an EXPLICIT seed rather than drawing from a
// shared RNG stream, so dealing a second library cannot perturb player 0's permutation by a single
// card. (Had it been a shared stream this change would have moved every deck's baseline and cost a
// full three-tier GT rebaseline.)
inline void Deal(GameState& state, uint64_t seed)
{
    if (!state.opponent_library_dealt) { return; }

    Player& opp = state.players[1];
    int number  = kNumberBase;
    for (const std::pair<const char*, int>& entry : List())
    {
        for (int i = 0; i < entry.second; ++i)
        {
            Card c;
            c.m_name = entry.first;
            c.RehashName();
            c.m_number = number++;
            opp.library.push_back(c);
        }
    }

    // 0xDEC7 ("deck"): any nonzero constant works -- what matters is that it is NOT the opening
    // salt, so the two shuffles are independent realisations of the same game seed.
    opp.library.Shuffle(SaltSeed(seed, 0xDEC7ull));

    // A flat 7. They never mulligan, because they never make a decision of any kind; the hand
    // exists so discard effects have something to bite on.
    for (int i = 0; i < kOpeningHandSize && !opp.library.empty(); ++i)
    {
        opp.hand.push_back(opp.library.front());
        opp.library.erase(opp.library.begin());
    }
}

// The opponent's DRAW, simulated at the end of each of OUR turns.
//
// There is no opponent turn to hang it on: the goldfish takes every turn and the opponent never
// becomes the active player, so GameEngine::DrawStep never runs for them. End-of-our-turn is the
// faithful slot either way -- on the play, their turn N follows our turn N; on the draw, their turn
// N+1 follows our turn N, and their turn 1 (where THEY are on the play) correctly draws nothing.
//
// It is also what makes the win turn come out right, with no special-casing anywhere. The user's
// rule (2026-09-02): "the win turn for the case where the opponent decks out should be listed as
// your last turn ... because the user doesn't get any main phases." Firing here sets
// opponent_decked while state.turn_number is still OUR turn, so the caller's ordinary win check
// reports that turn.
//
// Called from BOTH worlds -- the executor's end step and the rollout's turn boundary. A draw the
// rollout does not simulate is a deck-out the search cannot SEE, which leaves an executor that
// recognises a win it never steers toward: the Dragonstorm go-off failure exactly.
inline void EndOfTurnDraw(GameState& state)
{
    if (!state.opponent_library_dealt || state.opponent_decked) { return; }
    Player& opp = state.players[1];
    if (opp.library.empty())
    {
        // CR 104.3c: a player who would draw from an empty library loses the game the next time a
        // player would receive priority. Nothing here can respond, so it is immediate.
        state.opponent_decked = true;
        return;
    }
    opp.hand.push_back(opp.library.front());
    opp.library.erase(opp.library.begin());
}

} // namespace opponentdeck
