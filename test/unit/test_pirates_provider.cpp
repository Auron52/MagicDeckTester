// Unit cover for PiratesProvider (2026-09-26, Pirates onboarding chunk 3): the ROUTING and the tutor
// WIDTH.
//
// Routing is pinned over EVERY decklist in decks/, not just Pirates, because the failure this guards
// is the archetype-neutral misroute class -- a new signature silently capturing some other deck, or
// some other deck's signature capturing this one. The expected table is the provider every deck
// resolved to BEFORE PiratesProvider existed (scripts/provider_audit.py + a direct --batch probe for
// the three unprofiled lists), with exactly one change: Pirates itself, which used to land on
// AntiLifegain via Forerunner of the Coalition's tutor_to_top.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "cards/CardDatabase.h"
#include "core/HeuristicDefaults.h"
#include "deck/DeckLoader.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace
{

void EnsureCardsPp()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

}   // namespace

TEST_CASE("Pirates routing: every deck in decks/ resolves exactly as before, Pirates to PiratesProvider")
{
    EnsureCardsPp();
    const std::map<std::string, std::string> expected = {
        {"Angels", "Angels"},                 {"Anti-Lifegain", "AntiLifegain"},
        {"Auras", "Auras"},                   {"BreachingDragonstorm", "BreachingDragonstorm"},
        {"Creature Giving", "CreatureGiving"}, {"CritterLifegain", "CritterLifegain"},
        {"Dragons", "Dragons"},               {"Dragonstorm", "Dragonstorm"},
        {"EldraziDisplacerFlicker", "EldraziFlicker"}, {"FiveColour", "FiveColour"},
        {"Fluctuator", "Fluctuator"},         {"Fungus", "Fungus"},
        {"Giants", "Giants"},                 {"Goblins", "Goblins"},
        {"Hinata2", "Hinata"},                {"KittyEquipment", "Equipment"},
        {"Knights", "Knights"},               {"Melira Pod", "MeliraPod"},
        {"Minotaur", "Minotaur"},             {"Mirrorwing Dragon", "Mirrorwing"},
        {"Snow", "Snow"},                     {"StompySurprise", "Stompy"},
        {"burn", "Burn"},                     {"slivers_vial", "Vial"},
        {"treasure_hunt", "TreasureHunt"},
        // Unprofiled lists (no .profile.json): pinned from a direct --batch probe of the pre-change
        // binary so a future signature cannot capture them unnoticed either.
        {"Mill", "AntiLifegain"},             {"Unpredictable Cyclone", "AntiLifegain"},
        // THE ONE CHANGE: was AntiLifegain (Forerunner of the Coalition's tutor_to_top).
        {"Pirates", "Pirates"},
    };

    namespace fs = std::filesystem;
    const fs::path root = ResolveHeuristicDefaultsPath("decks");
    REQUIRE(fs::is_directory(root));
    int checked = 0;
    for (const auto& [folder, want] : expected)
    {
        fs::path deck;
        for (const char* ext : {".cod", ".txt"})
        {
            const fs::path cand = root / folder / (folder + ext);
            if (fs::exists(cand)) { deck = cand; break; }
        }
        REQUIRE_MESSAGE(!deck.empty(), "no decklist for ", folder);
        const DecisionProvider& p = DetectDecisionProvider(DeckLoader::LoadFromFile(deck));
        CHECK_MESSAGE(std::string(p.Name()) == want, folder, " routed to ", p.Name(), ", expected ", want);
        ++checked;
    }
    // A NEW deck folder must be added to the table above (its routing decided, not defaulted).
    int folders = 0;
    for (const auto& e : fs::directory_iterator(root)) { if (e.is_directory()) { ++folders; } }
    CHECK_MESSAGE(folders == checked, "decks/ holds ", folders, " folders but the table pins ", checked,
                  " -- add the new deck's expected provider here");
}

TEST_CASE("Pirates routing: the signature survives cutting any ONE of its five cards")
{
    EnsureCardsPp();
    const Decklist full = DeckLoader::LoadFromFile(ResolveHeuristicDefaultsPath("decks/Pirates/Pirates.cod"));
    for (const char* cut : {"Daring Buccaneer", "Kitesail Larcenist", "Dire Fleet Captain",
                            "Malcolm, the Eyes", "Forerunner of the Coalition"})
    {
        Decklist d = full;
        d.mainboard.erase(std::remove_if(d.mainboard.begin(), d.mainboard.end(),
                                         [&](const Card& c) { return c.m_name.str() == cut; }),
                          d.mainboard.end());
        CHECK_MESSAGE(std::string(DetectDecisionProvider(d).Name()) == "Pirates", "cutting ", cut);
    }
}

TEST_CASE("Pirates tutor width covers all nine Pirate names Forerunner can find")
{
    CHECK(PiratesProvider().TutorSearchWidth() >= 9);
    // The pinned base it replaces -- if the base ever widens past 9 this override is moot.
    CHECK(GenericProvider().TutorSearchWidth() == 6);
}
