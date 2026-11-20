// "A provider heuristic is REQUIRED" (src/ai/ProviderHeuristicRequired.h, docs/design/provider-heuristic-required.md).
// USER 2026-11-18 (Trench Gorger): "Maybe the best way to handle this is throwing an error if we don't implement a
// heuristic in the provider? Essentially we want the agent working on this to be forced to implement something
// because fully searched and what you mentioned are both unacceptable for normal usage."
// USER 2026-11-20 (supersedes the throw): "The default should be searched and probably the analysis should be given a
// warning if we don't override the policy. The expectation is that we will override it and so this should be a red
// flag."
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

Decklist PhDeck(bool gorger_main, bool gorger_side = false, const char* extra = nullptr)
{
    Decklist d;
    for (int i = 0; i < 40; ++i) { d.mainboard.push_back(PhCard("Island")); }   // no archetype signature: Generic
    if (gorger_main) { d.mainboard.push_back(PhCard("Trench Gorger")); }
    if (gorger_side) { d.sideboard.push_back(PhCard("Trench Gorger")); }
    if (extra != nullptr) { d.mainboard.push_back(PhCard(extra)); }
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

TEST_CASE("provider-required: the registry holds the USER's 2026-11-20 list, in enum order, each carried by its card")
{
    const std::vector<RequiredHeuristicSpec>& reg = RequiredHeuristicRegistry();
    REQUIRE(reg.size() == static_cast<std::size_t>(RequiredHeuristic::_Count));
    for (std::size_t i = 0; i < reg.size(); ++i) { CHECK(static_cast<std::size_t>(reg[i].id) == i); }
    const std::pair<const char*, RequiredHeuristic> cards[] = {
        { "Trench Gorger", RequiredHeuristic::ExileLibraryLands },
        { "Acclaimed Contender", RequiredHeuristic::EtbDigPick },
        { "Nykthos Paragon", RequiredHeuristic::LifegainCountersSpend },
        { "Genesis Wave", RequiredHeuristic::GenesisWavePut },
        { "Ranger of Eos", RequiredHeuristic::TutorHandPut },
        { "Dragonstorm", RequiredHeuristic::TutorToBattlefieldPut },
        { "Hedron Crab", RequiredHeuristic::HedronMillTarget },
        { "Reveillark", RequiredHeuristic::RevivePick },
    };
    for (const auto& [name, h] : cards)
    {
        CAPTURE(name);
        const CardDefinition* d = CardDatabase::Instance().LookupCached(PhCard(name));
        REQUIRE(d != nullptr);
        CHECK(RequiredHeuristicSpecOf(h).carried_by(d->params));
        CHECK_FALSE(PhGeneric().ImplementsRequiredHeuristic(h));
    }
    // USER 2026-11-20: "I don't think Breaching Dragonstorm has an issue because they don't play the same cards."
    const CardDefinition* bd = CardDatabase::Instance().LookupCached(PhCard("Breaching Dragonstorm"));
    REQUIRE(bd != nullptr);
    for (const RequiredHeuristicSpec& r : reg) { CHECK_FALSE(r.carried_by(bd->params)); }
}

TEST_CASE("provider-required: providers that already have a policy declare it")
{
    CHECK(PhSea().ImplementsRequiredHeuristic(RequiredHeuristic::ExileLibraryLands));
    CHECK(MillProvider().ImplementsRequiredHeuristic(RequiredHeuristic::HedronMillTarget));
    CHECK(DragonstormProvider().ImplementsRequiredHeuristic(RequiredHeuristic::TutorToBattlefieldPut));
    CHECK(MartyrLifegainProvider().ImplementsRequiredHeuristic(RequiredHeuristic::TutorHandPut));
    CHECK(MeliraPodProvider().ImplementsRequiredHeuristic(RequiredHeuristic::TutorHandPut));
    CHECK(MeliraPodProvider().ImplementsRequiredHeuristic(RequiredHeuristic::RevivePick));
    CHECK(PiratesProvider().ImplementsRequiredHeuristic(RequiredHeuristic::EtbDigPick));
    CHECK_FALSE(MillProvider().ImplementsRequiredHeuristic(RequiredHeuristic::EtbDigPick));
}

TEST_CASE("provider-required: a gap is a RED FLAG naming everything, never a refusal")
{
    const Decklist deck = PhDeck(true);
    const std::vector<std::string> gaps = RequiredHeuristicGaps(deck, PhGeneric());
    REQUIRE(gaps.size() == 1);   // one entry per (card, decision), however many copies
    CHECK(gaps[0].find("Trench Gorger") != std::string::npos);
    const std::string what = RequiredHeuristicMessage(gaps, PhGeneric(), "decks/Test/Test.cod");
    CHECK(what.find("RED FLAG") != std::string::npos);
    CHECK(what.find("Trench Gorger") != std::string::npos);                       // the card
    CHECK(what.find("decks/Test/Test.cod") != std::string::npos);                 // the deck
    CHECK(what.find("'Generic'") != std::string::npos);                           // the provider
    CHECK(what.find("RestrictExileLibraryLandsMenu") != std::string::npos);       // the hook
    CHECK(what.find("docs/design/provider-heuristic-required.md") != std::string::npos);   // the doc
    CHECK_NOTHROW(WarnRequiredHeuristics(deck, PhGeneric(), "decks/Test/Test.cod"));
    CHECK_NOTHROW(WarnRequiredHeuristics(PhDeck(false, false, "Acclaimed Contender"), PhGeneric(), "contender"));
    CHECK(RequiredHeuristicGaps(PhDeck(false, false, "Acclaimed Contender"), PhGeneric()).size() == 1);
}

TEST_CASE("provider-required: a sideboard (wish) Gorger counts too")
{
    CHECK(RequiredHeuristicGaps(PhDeck(false, true), PhGeneric()).size() == 1);
}

TEST_CASE("provider-required: the Sea provider and a Gorger-free deck carry no flag")
{
    CHECK(RequiredHeuristicGaps(PhDeck(true), PhSea()).empty());
    CHECK(RequiredHeuristicGaps(PhDeck(false), PhGeneric()).empty());
}

TEST_CASE("provider-required: the engine-site query -- a policy is live only where implemented")
{
    CHECK(std::string(GateName(UnprunedGate::HeuristicRequired)) == "heuristicrequired");
    if (DecisionUnpruned() || DecisionUnpruned(UnprunedGate::HeuristicRequired)) { return; }   // launched lifted
    CHECK(RequiredHeuristicPolicyLive(PhSea(), RequiredHeuristic::ExileLibraryLands));
    CHECK_FALSE(RequiredHeuristicPolicyLive(PhGeneric(), RequiredHeuristic::ExileLibraryLands));
    CHECK_FALSE(RequiredHeuristicPolicyLive(PhSea(), RequiredHeuristic::EtbDigPick));
}

TEST_CASE("provider-required: game setup plays a red-flagged deck (the throw is gone)")
{
    // SetupGame selects the provider by detection; an Island + Gorger list routes to Generic.
    const Decklist deck = PhDeck(true);
    REQUIRE(std::string(SelectDecisionProvider(deck).Name()) == "Generic");
    CHECK_NOTHROW((void)GoldFishRunner::SetupGame(deck, 1));
    CHECK_NOTHROW((void)GoldFishRunner::SetupGame(PhDeck(false), 1));
}
