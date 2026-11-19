// COMPLETE BOARD IDENTITY (src/ai/BoardIdentity.h): the equality the mana oracle's shared outcome boards rest on
// (docs/design/mana-oracle-board-memory-2026-11-19.md section 8).
//
// Sharing one held board between two outcomes is exact only if the two boards are equal in EVERY member a reader
// can reach -- a CoreKey match is not enough (it ignores the graveyard, exile and more). So:
//  1. A copy is equal and hashes equal.
//  2. A change to any member breaks equality: the zones CoreKey ignores (graveyard, exile, library order), the core
//     fields (tapped, counters, life, floating mana), and deep, rarely read members of every nested type
//     (Permanent, Player, StackEntry, the payment ledger, a GameState scalar).
//  3. The three LOGICAL equalities: a drawn library prefix, a stale counter slot and a stale subtype slot are
//     unreachable, so boards that differ only there are equal (and hash equal).
//  4. A cache pointer (Card::m_def) is compared as stored: it can only make boards UNEQUAL (costs sharing, never
//     exactness).
#include <doctest/doctest.h>

#include "ai/BoardIdentity.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <string>
#include <utility>

namespace
{
void EnsureCards()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card Named(const std::string& name, int num)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Card c = d->card;
    c.m_number = num;
    return c;
}

GameState Sample()
{
    EnsureCards();
    GameState s;
    s.active_player_index = 0;
    s.turn_number = 5;
    int n = 1;
    for (const char* name : { "Forest", "Island", "Llanowar Elves" })
    {
        Permanent p;
        p.card = Named(name, n++);
        p.controller_index = 0;
        p.owner_index = 0;
        s.battlefield.push_back(p);
    }
    s.players[0].hand.push_back(Named("Forest", n++));
    s.players[0].hand.push_back(Named("Llanowar Elves", n++));
    s.players[0].graveyard.push_back(Named("Island", n++));
    for (int i = 0; i < 6; ++i) { s.players[0].library.push_back(Named(i % 2 ? "Forest" : "Island", n++)); }
    s.exile.push_back(Named("Forest", n++));
    return s;
}

bool Same(const GameState& a, const GameState& b)
{
    const bool eq = boardid::BoardsEqual(a, b);
    if (eq) { CHECK(boardid::BoardHash(a) == boardid::BoardHash(b)); }   // equal boards must hash equal
    return eq;
}
}  // namespace

TEST_CASE("board identity: a copy is equal and hashes equal")
{
    const GameState a = Sample();
    const GameState b = a;
    CHECK(Same(a, b));
    CHECK(Same(b, a));
}

TEST_CASE("board identity: the zones CoreKey ignores break equality")
{
    const GameState a = Sample();
    {
        GameState b = a;
        b.players[0].graveyard.push_back(Named("Forest", 90));
        CHECK_FALSE(Same(a, b));
    }
    {
        GameState b = a;
        b.exile.clear();
        CHECK_FALSE(Same(a, b));
    }
    {
        GameState b = a;   // library ORDER: same cards, two swapped
        std::swap(b.players[0].library.RawAt(0), b.players[0].library.RawAt(1));
        CHECK_FALSE(Same(a, b));
    }
    {
        GameState b = a;   // the opponent's zones too
        b.players[1].graveyard.push_back(Named("Island", 91));
        CHECK_FALSE(Same(a, b));
    }
}

TEST_CASE("board identity: core fields break equality")
{
    const GameState a = Sample();
    { GameState b = a; b.battlefield[0].tapped = true;                          CHECK_FALSE(Same(a, b)); }
    { GameState b = a; b.battlefield[2].counters.Add(Counter::Type::PlusOnePlusOne, 1); CHECK_FALSE(Same(a, b)); }
    { GameState b = a; b.players[0].life = 19;                                  CHECK_FALSE(Same(a, b)); }
    { GameState b = a; b.floating_mana.green = 1;                               CHECK_FALSE(Same(a, b)); }
    { GameState b = a; b.spells_cast_this_turn = 1;                             CHECK_FALSE(Same(a, b)); }
}

TEST_CASE("board identity: deep members of every nested type break equality")
{
    const GameState a = Sample();
    { GameState b = a; b.battlefield[1].temp_double_strike = true;             CHECK_FALSE(Same(a, b)); }   // Permanent
    { GameState b = a; b.battlefield[1].card.m_staged_expiry = 7;               CHECK_FALSE(Same(a, b)); }   // Card
    { GameState b = a; b.players[0].bonus_land_drops_this_turn = 1;             CHECK_FALSE(Same(a, b)); }   // Player
    { GameState b = a; b.players[0].library.NoteKnownAt(0);                     CHECK_FALSE(Same(a, b)); }   // Library info
    { GameState b = a; b.prepay_wild_true.gtot = 3;                             CHECK_FALSE(Same(a, b)); }   // nested struct
    { GameState b = a; b.gy_shuffle_batch = 1;                                  CHECK_FALSE(Same(a, b)); }   // scalar
    {
        GameState b = a;   // StackEntry
        b.stack.emplace_back();
        GameState c = b;
        CHECK(Same(b, c));
        c.stack.back().chosen_x = 2;
        CHECK_FALSE(Same(b, c));
    }
}

TEST_CASE("board identity: unreachable storage does not count (drawn prefix, stale counter and subtype slots)")
{
    GameState a = Sample();
    (void)a.players[0].library.DrawTop();   // a drawn prefix below m_top
    GameState b = a;                  // a copy drops it
    CHECK(Same(a, b));

    GameState c = Sample();
    c.battlefield[2].counters.Add(Counter::Type::PlusOnePlusOne, 2);
    c.battlefield[2].counters.Add(Counter::Type::Loyalty, 3);
    GameState d = c;
    c.battlefield[2].counters.erase(c.battlefield[2].counters.begin() + 1);   // the Loyalty slot is now stale
    d.battlefield[2].counters.clear();
    d.battlefield[2].counters.Add(Counter::Type::PlusOnePlusOne, 2);
    CHECK(Same(c, d));

    GameState e = Sample();
    GameState f = e;
    e.battlefield[2].card.m_subtypes.push_back("Druid");
    e.battlefield[2].card.m_subtypes.clear();
    f.battlefield[2].card.m_subtypes.clear();
    CHECK(Same(e, f));
}

TEST_CASE("board identity: a cache pointer can only separate boards (Card::m_def compared as stored)")
{
    const GameState a = Sample();
    GameState b = a;
    b.battlefield[0].card.m_def = nullptr;
    GameState c = a;
    CardDatabase::Instance().LookupCached(c.battlefield[0].card);
    if (b.battlefield[0].card.m_def != c.battlefield[0].card.m_def) { CHECK_FALSE(Same(b, c)); }
    GameState d = c;
    CHECK(Same(c, d));
}
