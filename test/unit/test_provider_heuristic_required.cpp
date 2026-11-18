// "A provider heuristic is REQUIRED" (src/ai/ProviderHeuristicRequired.h, docs/design/provider-heuristic-required.md).
// USER 2026-11-18 (Trench Gorger): "Maybe the best way to handle this is throwing an error if we don't implement a
// heuristic in the provider? Essentially we want the agent working on this to be forced to implement something
// because fully searched and what you mentioned are both unacceptable for normal usage."
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/ProviderHeuristicRequired.h"
#include "cards/CardDatabase.h"
#include "core/HeuristicDefaults.h"
#include "deck/DeckLoader.h"
#include "runner/GoldFishRunner.h"

#include <stdexcept>
#include <string>

namespace
{
Card PhCard(const std::string& name)
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return d->card;
}

Decklist PhDeck(bool gorger_main, bool gorger_side = false)
{
    Decklist d;
    for (int i = 0; i < 40; ++i) { d.mainboard.push_back(PhCard("Island")); }   // no archetype signature: Generic
    if (gorger_main) { d.mainboard.push_back(PhCard("Trench Gorger")); }
    if (gorger_side) { d.sideboard.push_back(PhCard("Trench Gorger")); }
    return d;
}

const GenericProvider& PhGeneric()
{
    static const GenericProvider p;
    return p;
}

const SeaCreaturesProvider& PhSea()
{
    static const SeaCreaturesProvider p;
    return p;
}
}  // namespace

TEST_CASE("provider-required: Trench Gorger is registered; the generic provider does not implement it, Sea does")
{
    bool found = false;
    for (const RequiredHeuristicSpec& r : RequiredHeuristicRegistry())
    {
        if (r.id == RequiredHeuristic::ExileLibraryLands)
        {
            found = true;
            CHECK(std::string(r.param) == "etb_exile_library_lands_set_base_pt");
        }
    }
    CHECK(found);
    CHECK_FALSE(PhGeneric().ImplementsRequiredHeuristic(RequiredHeuristic::ExileLibraryLands));
    CHECK(PhSea().ImplementsRequiredHeuristic(RequiredHeuristic::ExileLibraryLands));
}

TEST_CASE("provider-required: a Gorger deck under the generic provider is refused with a message naming everything")
{
    const Decklist deck = PhDeck(true);
    const std::vector<std::string> gaps = RequiredHeuristicGaps(deck, PhGeneric());
    REQUIRE(gaps.size() == 1);   // one entry per (card, decision), however many copies
    CHECK(gaps[0].find("Trench Gorger") != std::string::npos);
    std::string what;
    try { EnforceRequiredHeuristics(deck, PhGeneric(), "decks/Test/Test.cod", nullptr); }
    catch (const std::runtime_error& e) { what = e.what(); }
    REQUIRE_FALSE(what.empty());
    CHECK(what.find("Trench Gorger") != std::string::npos);                       // the card
    CHECK(what.find("decks/Test/Test.cod") != std::string::npos);                 // the deck
    CHECK(what.find("'Generic'") != std::string::npos);                           // the provider
    CHECK(what.find("RestrictExileLibraryLandsMenu") != std::string::npos);       // the hook
    CHECK(what.find("docs/design/provider-heuristic-required.md") != std::string::npos);   // the doc
    CHECK(what.find("MTG_UNPRUNE=heuristicrequired") != std::string::npos);       // the bypasses
}

TEST_CASE("provider-required: a sideboard (wish) Gorger counts too")
{
    CHECK(RequiredHeuristicGaps(PhDeck(false, true), PhGeneric()).size() == 1);
}

TEST_CASE("provider-required: the Sea provider and a Gorger-free deck run")
{
    CHECK(RequiredHeuristicGaps(PhDeck(true), PhSea()).empty());
    CHECK(RequiredHeuristicGaps(PhDeck(false), PhGeneric()).empty());
    CHECK_NOTHROW(EnforceRequiredHeuristics(PhDeck(true), PhSea(), "sea", nullptr));
    CHECK_NOTHROW(EnforceRequiredHeuristics(PhDeck(false), PhGeneric(), "plain", nullptr));
}

TEST_CASE("provider-required: human play and the unprune route bypass the refusal (with a warning)")
{
    CHECK_NOTHROW(EnforceRequiredHeuristics(PhDeck(true), PhGeneric(), "viewer", "MTG_HUMAN_PLAY"));
    CHECK_NOTHROW(EnforceRequiredHeuristics(PhDeck(true), PhGeneric(), "measure", "MTG_UNPRUNED"));
    CHECK_NOTHROW(EnforceRequiredHeuristics(PhDeck(true), PhGeneric(), "measure", "MTG_UNPRUNE=heuristicrequired"));
    CHECK(std::string(GateName(UnprunedGate::HeuristicRequired)) == "heuristicrequired");
}

TEST_CASE("provider-required: game setup refuses the Gorger deck under the generic provider")
{
    // SetupGame selects the provider by detection; an Island + Gorger list routes to Generic.
    const Decklist deck = PhDeck(true);
    REQUIRE(std::string(SelectDecisionProvider(deck).Name()) == "Generic");
    if (RequiredHeuristicBypass() != nullptr) { return; }   // the test binary was launched under a bypass env
    CHECK_THROWS_AS((void)GoldFishRunner::SetupGame(deck, 1), std::runtime_error);
    CHECK_NOTHROW((void)GoldFishRunner::SetupGame(PhDeck(false), 1));
}
