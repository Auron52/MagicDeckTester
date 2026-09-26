// Keyword-mask overflow regression (2026-09-26). enum Keyword grew past 32 values while
// Card::m_keyword_mask was a uint32_t and Bit(Keyword) was `1u << k`, so Persist(32) / Evoke(33) /
// Convoke(34) were undefined behaviour -- on x86 the shift count is masked to 5 bits and they
// silently aliased Haste / Flying / Trample. Kitchen Finks and Murderous Redcap (Persist) thereby
// carried PHANTOM HASTE into every Melira Pod game. These pin the fix at the card-data level.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/HeuristicDefaults.h"

namespace
{
const Card& PrintedCard(const std::string& name)
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return d->card;
}
}   // namespace

TEST_CASE("Keyword mask: Persist does not alias Haste (Kitchen Finks, Murderous Redcap)")
{
    for (const char* n : {"Kitchen Finks", "Murderous Redcap"})
    {
        const Card& c = PrintedCard(n);
        CHECK(c.HasKeyword(Keyword::Persist));
        CHECK_FALSE(c.HasKeyword(Keyword::Haste));
    }
}

TEST_CASE("Keyword mask: every enumerator owns a distinct bit")
{
    for (int i = 0; i < static_cast<int>(Keyword::KeywordCount_); ++i)
    {
        Card c;
        c.AddKeyword(static_cast<Keyword>(i));
        for (int j = 0; j < static_cast<int>(Keyword::KeywordCount_); ++j)
        { CHECK(c.HasKeyword(static_cast<Keyword>(j)) == (i == j)); }
    }
}
