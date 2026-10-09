// Unit tests for CounterList's CANONICAL storage (2026-10-09): at most one entry per Counter::Type.
//
// A Counter is only (type, count), so two +1/+1 entries are game-identical to one entry holding their
// sum (CR 122.1). Before this, seven raw sites APPENDED a second +1/+1 entry, so .size() and the
// ordered (type, count) list -- both read by BuildSimKey, the dominance signature and several dedup
// keys -- differed between game-identical boards; and a loop that re-fires a raw site (Melira Pod:
// Carrion Feeder eats a persist creature, Celes counters the team) overflowed the six-slot inline
// array, which then merged entries and moved .size() mid-game.
//
//  1. Add merges same-type puts and appends only a new type; insertion order of TYPES is kept.
//  2. A thousand puts of one type is one entry and never touches the overflow path.
//  3. AnnihilateCounters (CR 704.5q) on merged storage leaves the right totals.
//  4. A real raw site (Carrion Feeder's sacrifice payload) puts into the existing +1/+1 entry.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/SpellEffects.h"
#include "core/HeuristicDefaults.h"

#include <string>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        const auto path = ResolveHeuristicDefaultsPath("src/cards/data/cards.json");
        CardDatabase::Instance().LoadFromJson(path);
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& Def(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

int Put(GameState& s, const std::string& name, int controller, int number)
{
    Permanent p;
    p.card              = Def(name).card;
    p.card.m_number     = number;
    p.controller_index  = controller;
    p.owner_index       = controller;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

int CountOf(const CounterList& cl, Counter::Type t)
{
    int n = 0;
    for (const Counter& c : cl) { if (c.type == t) { n += c.count; } }
    return n;
}

}   // namespace

TEST_CASE("CounterList::Add merges a type into its existing entry and appends only a new type")
{
    CounterList cl;
    cl.Add(Counter::Type::PlusOnePlusOne, 1);
    cl.Add(Counter::Type::Depletion, 2);
    cl.Add(Counter::Type::PlusOnePlusOne, 3);
    cl.Add(Counter{Counter::Type::PlusOnePlusOne, 1});
    REQUIRE(cl.size() == 2);
    CHECK(cl[0].type == Counter::Type::PlusOnePlusOne);   // first-seen type order is kept
    CHECK(cl[0].count == 5);
    CHECK(cl[1].type == Counter::Type::Depletion);
    CHECK(cl[1].count == 2);
}

TEST_CASE("CounterList: every type at once fits, and a thousand puts never overflow")
{
    const std::uint64_t before = g_counter_overflows.load();
    CounterList cl;
    for (int i = 0; i < 1000; ++i)
    {
        cl.Add(Counter::Type::PlusOnePlusOne, 1);
        cl.Add(Counter::Type::MinusOneMinusOne, 1);
        cl.Add(Counter::Type::Loyalty, 1);
        cl.Add(Counter::Type::Poison, 1);
        cl.Add(Counter::Type::Depletion, 1);
    }
    CHECK(cl.size() == 5);
    for (const Counter& c : cl) { CHECK(c.count == 1000); }
    CHECK(g_counter_overflows.load() == before);
}

TEST_CASE("Game-identical counter histories produce identical CounterLists")
{
    Permanent a, b;
    for (int i = 0; i < 9; ++i) { a.counters.Add(Counter::Type::PlusOnePlusOne, 1); }
    b.counters.Add(Counter::Type::PlusOnePlusOne, 4);
    b.counters.Add(Counter::Type::PlusOnePlusOne, 5);
    REQUIRE(a.counters.size() == b.counters.size());
    for (std::size_t k = 0; k < a.counters.size(); ++k)
    {
        CHECK(a.counters[k].type == b.counters[k].type);
        CHECK(a.counters[k].count == b.counters[k].count);
    }
    CHECK(a.EffectivePower() == b.EffectivePower());
}

TEST_CASE("AnnihilateCounters on merged storage: +1/+1 and -1/-1 cancel pairwise (CR 704.5q)")
{
    Permanent p;
    p.counters.Add(Counter::Type::PlusOnePlusOne, 2);
    p.counters.Add(Counter::Type::PlusOnePlusOne, 3);
    p.counters.Add(Counter::Type::MinusOneMinusOne, 4);
    AnnihilateCounters(p);
    REQUIRE(p.counters.size() == 1);
    CHECK(p.counters[0].type == Counter::Type::PlusOnePlusOne);
    CHECK(p.counters[0].count == 1);
    p.counters.Add(Counter::Type::MinusOneMinusOne, 1);
    AnnihilateCounters(p);
    CHECK(p.counters.empty());
}

TEST_CASE("Carrion Feeder's sacrifice payload puts into the existing +1/+1 entry")
{
    EnsureCardsLoaded();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    const int feeder = Put(s, "Carrion Feeder", 0, 1);
    s.battlefield[feeder].counters.Add(Counter::Type::PlusOnePlusOne, 2);
    for (int k = 0; k < 8; ++k) { Put(s, "Bloodthrone Vampire", 0, 100 + k); }
    for (int k = 0; k < 8; ++k) { ApplySacCreatureOutlet(s, 0, /*source_id=*/1, /*victim_id=*/100 + k); }
    const Permanent* f = nullptr;
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == 1) { f = &p; } }
    REQUIRE(f != nullptr);
    CHECK(f->counters.size() == 1);
    CHECK(CountOf(f->counters, Counter::Type::PlusOnePlusOne) == 10);
}

TEST_CASE("CounterList::KeyFragment keys per-type TOTALS, independent of put order")
{
    CounterList a, b, c, none;
    a.Add(Counter::Type::Depletion, 2);
    a.Add(Counter::Type::PlusOnePlusOne, 1);
    b.Add(Counter::Type::PlusOnePlusOne, 1);
    b.Add(Counter::Type::Depletion, 2);
    CHECK(a.KeyFragment() == b.KeyFragment());           // same totals, other first-put order
    c.Add(Counter::Type::PlusOnePlusOne, 5);
    c.Add(Counter::Type::Depletion, 2);
    CHECK(c.size() == a.size());                          // size() cannot tell them apart...
    CHECK(c.KeyFragment() != a.KeyFragment());            // ...the fragment can
    CHECK(none.KeyFragment().empty());
    CounterList depleted;                                 // a spent depletion land keeps its 0 marker
    depleted.Add(Counter::Type::Depletion, 0);
    CHECK(depleted.KeyFragment() != none.KeyFragment());
}
