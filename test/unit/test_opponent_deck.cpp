// Unit tests for the passive opponent's library, hand, and deck-out (src/core/OpponentDeck.h).
//
// Three things here are easy to get wrong in a way no aggregate fingerprint would catch:
//
//  1. EVERY NAME MUST RESOLVE. A library card is a bare placeholder -- its types come from the
//     CardDatabase at read time. If a name in the fixed list is renamed or dropped from cards.json,
//     the lookup silently returns null and the card becomes typeless: Dimensional Infiltrator's
//     "if it's a land card" check would then see NO lands at all, quietly halving the behaviour it
//     models, and every game would still run to completion looking perfectly healthy.
//
//  2. THE LAND RATIO IS READ, not decoration. Same reason: the Infiltrator's land branch fires at
//     the list's land frequency, so a drifted ratio silently changes what that card does.
//
//  3. EMPTY IS NOT ABSENT. `players[1].library` is empty on every deck that was not dealt one, so
//     the obvious `library.empty()` deck-out test is TRUE on turn 0 -- a spurious instant win in
//     every game of every deck. The gate is `opponent_library_dealt`, and the test below pins that
//     an UNDEALT opponent never reads as lost no matter how many draws are simulated.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/OpponentDeck.h"

#include <string>

namespace {

int TotalCards()
{
    int n = 0;
    for (const std::pair<const char*, int>& e : opponentdeck::List()) { n += e.second; }
    return n;
}

} // namespace

TEST_CASE("opponent deck: every card resolves in the database")
{
    for (const std::pair<const char*, int>& e : opponentdeck::List())
    {
        Card c;
        c.m_name = e.first;
        c.RehashName();
        const CardDefinition* def = CardDatabase::Instance().LookupCached(c);
        INFO("card: " << e.first);
        REQUIRE(def != nullptr);
    }
}

TEST_CASE("opponent deck: 60 cards, and the land ratio the Infiltrator reads")
{
    CHECK(TotalCards() == 60);

    int lands = 0;
    for (const std::pair<const char*, int>& e : opponentdeck::List())
    {
        Card c;
        c.m_name = e.first;
        c.RehashName();
        const CardDefinition* def = CardDatabase::Instance().LookupCached(c);
        REQUIRE(def != nullptr);
        // The DEFINITION's card, never the zone placeholder: a card outside the battlefield has
        // empty type masks, so `c.IsLand()` here would be false for all 60.
        if (def->card.IsLand()) { lands += e.second; }
    }
    CHECK(lands == 24);
}

TEST_CASE("opponent deck: dealing is gated, and leaves 53 in library / 7 in hand")
{
    // UNDEALT (every deck that cannot touch the opponent's zones): Deal is a no-op, and no number
    // of simulated draws may ever make the opponent read as lost.
    {
        GameState s;
        opponentdeck::Deal(s, /*needed=*/false, 12345);
        CHECK_FALSE(s.opponent_library_dealt);   // Deal is the ONLY writer of the flag
        CHECK(s.players[1].library.size() == 0u);
        CHECK(s.players[1].hand.empty());
        for (int i = 0; i < 50; ++i) { opponentdeck::EndOfTurnDraw(s); }
        CHECK_FALSE(s.opponent_decked);
        CHECK_FALSE(OpponentHasLost(s));
    }

    // DEALT: 60 - 7 = 53 left, which is the constant that makes a mill measurement comparable
    // across decks.
    {
        GameState s;
        opponentdeck::Deal(s, /*needed=*/true, 12345);
        CHECK(s.opponent_library_dealt);
        CHECK(s.players[1].hand.size() == static_cast<std::size_t>(opponentdeck::kOpeningHandSize));
        CHECK(s.players[1].library.size() == 53u);
    }
}

TEST_CASE("opponent deck: the shuffle is seed-derived, not a shared stream")
{
    // Two different game seeds must give different opponent orders (otherwise the derived salt is
    // doing nothing), and the SAME seed must reproduce exactly (otherwise the game is not
    // deterministic). The reason the salt matters at all: player 0's shuffle must be unaffected,
    // which is what kept this change byte-identical for every existing deck.
    GameState a, b, c;
    opponentdeck::Deal(a, /*needed=*/true, 1);
    opponentdeck::Deal(b, /*needed=*/true, 2);
    opponentdeck::Deal(c, /*needed=*/true, 1);

    std::string sa, sb, sc;
    for (const Card& x : a.players[1].library) { sa += x.m_name.str(); sa += ';'; }
    for (const Card& x : b.players[1].library) { sb += x.m_name.str(); sb += ';'; }
    for (const Card& x : c.players[1].library) { sc += x.m_name.str(); sc += ';'; }
    CHECK(sa == sc);
    CHECK(sa != sb);
}

TEST_CASE("opponent deck: TakeFromTop decks them the INSTANT the library empties")
{
    // The asymmetry with the draw is deliberate and load-bearing (see the comment on TakeFromTop).
    // A mill that empties the library during OUR turn T loses them the game at the end of turn T
    // either way -- but only the immediate flag lets the SEARCH see it, because wins_this_turn and
    // every mid-turn OpponentHasLost check run before that end-of-turn draw.
    GameState s;
    opponentdeck::Deal(s, /*needed=*/true, 3);
    REQUIRE(s.players[1].library.size() == 53u);

    CHECK(opponentdeck::TakeFromTop(s, 52) == 52);
    CHECK(s.players[1].library.size() == 1u);
    CHECK_FALSE(OpponentHasLost(s));
    CHECK(s.exile.size() == 52u);          // exiled, not milled to a graveyard (CR 406.1)

    CHECK(opponentdeck::TakeFromTop(s, 1) == 1);
    CHECK(s.players[1].library.size() == 0u);
    CHECK(OpponentHasLost(s));             // immediate, NOT deferred to the next draw

    // Over-asking is clamped to what is there, and never negative.
    CHECK(opponentdeck::TakeFromTop(s, 10) == 0);

    // Gated like everything else: a deck that was never dealt a library cannot be milled into a
    // spurious win.
    GameState u;   // never dealt -> the flag is false -> milling cannot manufacture a win
    CHECK(opponentdeck::TakeFromTop(u, 10) == 0);
    CHECK_FALSE(OpponentHasLost(u));
}

TEST_CASE("perm abilities: the three {T}-less modes are not tap-gated")
{
    // Getting this wrong taps the source on every activation, which destroys exactly the
    // repeatability that makes the two Eldrazi mana sinks win conditions -- and blocks activation
    // on a summoning-sick or attacking body, both of which are legal (CR 302.6 restricts {T}
    // abilities and attacking; neither applies to a costed ability with no tap symbol).
    CHECK(PermAbilityTaps(PermAbilityMode::TapDamage));
    CHECK(PermAbilityTaps(PermAbilityMode::TapInvestigate));
    CHECK(PermAbilityTaps(PermAbilityMode::TapDraw));
    CHECK_FALSE(PermAbilityTaps(PermAbilityMode::SacDraw));
    CHECK_FALSE(PermAbilityTaps(PermAbilityMode::Drain));
    CHECK_FALSE(PermAbilityTaps(PermAbilityMode::ExileTop));
}

TEST_CASE("opponent deck: deck-out fires on the draw from an empty library")
{
    GameState s;
    opponentdeck::Deal(s, /*needed=*/true, 7);

    // 53 draws empty the library without decking anyone -- running OUT is not the loss; being
    // ASKED to draw from empty is (CR 104.3c).
    for (int i = 0; i < 53; ++i) { opponentdeck::EndOfTurnDraw(s); }
    CHECK(s.players[1].library.size() == 0u);
    CHECK_FALSE(s.opponent_decked);
    CHECK_FALSE(OpponentHasLost(s));

    // The 54th is the one that loses the game.
    opponentdeck::EndOfTurnDraw(s);
    CHECK(s.opponent_decked);
    CHECK(OpponentHasLost(s));
}
