// Unit tests for the SUBTYPE REGISTRY's post-load immutability.
//
// Why this file exists. SubtypeRegistry's header has always claimed "Intern() runs only at load
// (single-threaded)", and Id()/Name() are lock-free hot-path reads that are only safe if that
// claim holds. It did not hold: a handful of subtype names exist ONLY as C++ string literals in
// effect code -- Oko's "Elk" and "Food" tokens, the Kavu token -- so they were absent from
// cards.json and absent from the registry until the first worker thread that created one interned
// it MID-SEARCH. That call does `m_names.push_back(...)`, a std::vector reallocation, while other
// workers are reading that same vector in Name(id). ThreadSanitizer flagged it on 12 threads
// (write at SubtypeRegistry.h:44 from ApplyLoyaltyAbility vs read at :59 from CardHasSubtype):
// real UB -- a torn read or a freed buffer, not a benign same-value write.
//
// The fix is only as good as its list, so the list is what gets tested. A newly added token whose
// subtype is a literal must be added to SubtypeRegistry::RuntimeLiterals(); if it is not, it will
// resolve to kNone at runtime instead of racing -- which is safe but wrong, and silent. These
// tests make it loud at build time instead.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/SubtypeRegistry.h"
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

}   // namespace

TEST_CASE("subtype registry is frozen after the card database loads")
{
    EnsureCardsLoaded();
    CHECK(SubtypeRegistry::Instance().Frozen());
}

TEST_CASE("every code-only subtype literal resolves after load")
{
    EnsureCardsLoaded();
    const SubtypeRegistry& reg = SubtypeRegistry::Instance();

    // The whole point: each of these must already have an id, so the runtime token-creation path
    // (SubtypeSet::operator= -> push_back -> Intern) is a pure lookup and never mutates.
    for (const std::string& lit : SubtypeRegistry::RuntimeLiterals())
    {
        const uint16_t id = reg.Id(lit);
        CHECK_MESSAGE(id != SubtypeRegistry::kNone,
                      "runtime subtype literal not interned at load: ", lit);
        // Round-trips: the id names the string it was interned from.
        CHECK(reg.Name(id) == lit);
    }
}

TEST_CASE("a frozen registry refuses to mint new ids")
{
    EnsureCardsLoaded();
    SubtypeRegistry& reg = SubtypeRegistry::Instance();
    REQUIRE(reg.Frozen());

    // A name no card and no effect uses. Interning it post-freeze must NOT allocate an id (that is
    // the mutation that raced); it returns kNone and warns once on stderr.
    const std::string bogus = "ZzNotARealSubtypeZz";
    REQUIRE(reg.Id(bogus) == SubtypeRegistry::kNone);
    CHECK(reg.Intern(bogus) == SubtypeRegistry::kNone);
    CHECK(reg.Id(bogus) == SubtypeRegistry::kNone);   // still absent -> nothing was inserted
}

TEST_CASE("known cards.json subtypes still resolve")
{
    EnsureCardsLoaded();
    const SubtypeRegistry& reg = SubtypeRegistry::Instance();

    // Guards the freeze against the opposite failure: freezing too early / thawing incorrectly
    // would leave ordinary card subtypes unresolvable, which would silently break every
    // "is this a Goblin?" tribal check rather than raising anything.
    for (const char* s : { "Goblin", "Sliver", "Minotaur", "Mountain" })
    {
        CHECK_MESSAGE(reg.Id(s) != SubtypeRegistry::kNone, "cards.json subtype missing: ", s);
    }
}
