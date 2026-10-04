// Unit cover for PiratesProvider (2026-09-26, Pirates onboarding chunk 3): the ROUTING, the tutor
// WIDTH, and the authored cleanup-discard BUCKETS.
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
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "deck/DeckLoader.h"

#include <algorithm>
#include <filesystem>
#include <cctype>
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

Card MakeCardPp(const std::string& name, int number)
{
    const CardDefinition* def = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(def != nullptr, "card not in cards.json: ", name);
    Card c     = def->card;
    c.m_number = number;
    return c;
}

// A hand card is a NAME-ONLY placeholder in real play (DeckLoader::MakePlaceholder), so the hand is
// built that way here too -- a policy that read the hand card's own masks would pass a test built
// from full cards and fail in the engine.
Card PlaceholderPp(const std::string& name, int number)
{
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

GameState MakeState(const std::vector<std::string>& hand, const std::vector<std::string>& board,
                    int vial_counters = 0)
{
    EnsureCardsPp();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    int num = 1;
    for (const std::string& n : hand) { s.players[0].hand.push_back(PlaceholderPp(n, num++)); }
    for (const std::string& n : board)
    {
        Permanent p;
        p.card              = MakeCardPp(n, num++);
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        if (n == "Aether Vial") { p.charge_counters = vial_counters; }
        s.battlefield.push_back(p);
    }
    return s;
}

std::vector<int> Rank(const GameState& s) { return PiratesProvider().CleanupDiscardCandidates(s, nullptr); }

std::string First(const GameState& s, const std::vector<int>& order)
{
    REQUIRE_FALSE(order.empty());
    return s.players[0].hand[order[0]].m_name.str();
}

int ShedPos(const GameState& s, const std::vector<int>& order, const std::string& name)
{
    for (int k = 0; k < static_cast<int>(order.size()); ++k)
    { if (s.players[0].hand[order[k]].m_name.str() == name) { return k; } }
    return -1;
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
        // WhiteKnights (adopted 2026-09-26 on the other machine; met this table at the 2026-09-27
        // rebase): the second shipping Knight list, routed by its own signature ABOVE Knights --
        // see the WhiteKnightsProvider note in DecisionProviders.cpp.
        {"WhiteKnights", "WhiteKnights"},
        // Unprofiled lists (no .profile.json): pinned from a direct --batch probe of the pre-change
        // binary so a future signature cannot capture them unnoticed either.
        {"Mill", "AntiLifegain"},             {"Unpredictable Cyclone", "AntiLifegain"},
        // THE ONE CHANGE: was AntiLifegain (Forerunner of the Coalition's tutor_to_top).
        {"Pirates", "Pirates"},
        // Prevent Damage (onboarded 2026-09-27): routed at the TOP by its own signature (Tamanoa /
        // Manabarbs / Vito / Dina) -- it would otherwise ride AntiLifegain via Living Wish's
        // tutor_to_hand. See the PreventDamageProvider note in DecisionProviders.h.
        {"Prevent Damage", "PreventDamage"},
        // SelesnyaLifegain (onboarded 2026-09-30): its OWN provider, routed at the top of the chain.
        // This pin previously read "Stompy", recording a MEASURED capture: Craterhoef's
        // etb_team_pump_per_creature and Priest of Titania / Elvish Archdruid's creature-side
        // mana_per_creature_subtype EACH set `stompy` alone, so the deck inherited StompyProvider's
        // cast order and cleanup-discard buckets from the moment its first card was implemented.
        // It also had to clear `critter` (returned above `stompy`), which a naive Ageless Entity /
        // Nykthos Paragon modelling would have set. Both are now cleared two ways: the deck has its
        // own nine-param signature routed above both, AND neither payoff card uses the legacy ints
        // (`*_that_many` bools instead), so the critter signature is never set in the first place.
        // SelesnyaLifegainProvider is an EMPTY DeckProvider derivation -- play-neutral by
        // construction -- so this change of routing is a change of NAME and of future extension
        // point, not of play. See its declaration in DecisionProviders.h.
        {"SelesnyaLifegain", "SelesnyaLifegain"},
        // Soldiers (onboarding 2026-10-04): its own `soldiers` signature (Harbin / Thalia / Thalia's
        // Lieutenant / Recruitment Officer / Rick params) routes ABOVE goblin (three sac
        // outlets) and anti (two creature tutors) -- and the Angels term it used to trip (Champion of
        // the Parish's self-counter watcher) is now keyed on an ["Angel"] filter. Stage 4a gave it
        // SoldiersProvider (an empty derivation of VialProvider) so it no longer shares slivers_vial's.
        {"Soldiers", "Soldiers"},
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
            // decks/Soldiers/soldiers.cod: a lower-cased list name inside its folder.
            std::string lower = folder;
            for (char& ch : lower) { ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); }
            const fs::path cand_lc = root / folder / (lower + ext);
            if (fs::exists(cand_lc)) { deck = cand_lc; break; }
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

TEST_CASE("Pirates discard: a surplus land is shed before any threat")
{
    // Four lands on board already meet the source target, so the Mountain is pure overflow.
    const GameState s = MakeState(
        {"Siren Stormtamer", "Daring Buccaneer", "Goblin Tomb Raider", "Staunch Crewmate",
         "Corsair Captain", "Metallic Mimic", "Dire Fleet Captain", "Mountain"},
        {"Mountain", "Spirebluff Canal", "Secluded Courtyard", "Blackcleave Cliffs"});
    CHECK(First(s, Rank(s)) == "Mountain");
}

TEST_CASE("Pirates discard: with no surplus mana, the weakest threat goes -- never the lord")
{
    // The max-MV fallback would pitch a 3-drop (Corsair Captain / Adaptive Automaton / Forerunner).
    const GameState s = MakeState(
        {"Siren Stormtamer", "Daring Buccaneer", "Goblin Tomb Raider", "Staunch Crewmate",
         "Corsair Captain", "Adaptive Automaton", "Forerunner of the Coalition", "Metallic Mimic"},
        {"Mountain", "Spirebluff Canal", "Secluded Courtyard", "Blackcleave Cliffs"});
    const std::vector<int> order = Rank(s);
    CHECK(First(s, order) == "Siren Stormtamer");
    CHECK(ShedPos(s, order, "Corsair Captain") > ShedPos(s, order, "Goblin Tomb Raider"));
    CHECK(ShedPos(s, order, "Adaptive Automaton") > ShedPos(s, order, "Staunch Crewmate"));
    // The two lords are the last two cards the policy would let go.
    CHECK(ShedPos(s, order, "Corsair Captain") >= 6);
    CHECK(ShedPos(s, order, "Adaptive Automaton") >= 6);
}

TEST_CASE("Pirates discard: a Vial on board makes a second Vial the first thing shed")
{
    const GameState s = MakeState(
        {"Aether Vial", "Siren Stormtamer", "Daring Buccaneer", "Goblin Tomb Raider",
         "Staunch Crewmate", "Corsair Captain", "Metallic Mimic", "Kitesail Larcenist"},
        {"Aether Vial", "Mountain", "Spirebluff Canal", "Secluded Courtyard", "Fiery Islet"}, 1);
    CHECK(First(s, Rank(s)) == "Aether Vial");
}

TEST_CASE("Pirates discard: mana-starved (2 lands reachable, no Vial on board) the Vial is KEPT over a weak threat")
{
    const GameState s = MakeState(
        {"Aether Vial", "Siren Stormtamer", "Daring Buccaneer", "Goblin Tomb Raider",
         "Staunch Crewmate", "Corsair Captain", "Metallic Mimic", "Spirebluff Canal"},
        {"Mountain"});
    const std::vector<int> order = Rank(s);
    CHECK(ShedPos(s, order, "Aether Vial") > ShedPos(s, order, "Siren Stormtamer"));
    CHECK(ShedPos(s, order, "Spirebluff Canal") > ShedPos(s, order, "Siren Stormtamer"));
}

TEST_CASE("Pirates discard: among surplus lands the narrowest goes first, a Fiery Islet last")
{
    const GameState s = MakeState(
        {"Mountain", "Fiery Islet", "Spirebluff Canal", "Corsair Captain", "Metallic Mimic",
         "Dire Fleet Captain", "Staunch Crewmate", "Adaptive Automaton"},
        {"Mountain", "Spirebluff Canal", "Secluded Courtyard", "Blackcleave Cliffs"});
    const std::vector<int> order = Rank(s);
    CHECK(First(s, order) == "Mountain");
    CHECK(ShedPos(s, order, "Fiery Islet") > ShedPos(s, order, "Spirebluff Canal"));
    CHECK(ShedPos(s, order, "Fiery Islet") < ShedPos(s, order, "Staunch Crewmate"));
}

TEST_CASE("Pirates discard: a colour-fixing land is kept over a redundant one")
{
    // One Mountain on board, hand wants U (Crewmate, Corsair) and B (Dire Fleet Captain). The land
    // that adds a missing colour is kept; the second Mountain is the surplus. Need = 3 more sources,
    // hand holds 4 lands -> exactly one land is overflow.
    const GameState s = MakeState(
        {"Mountain", "Spirebluff Canal", "Blackcleave Cliffs", "Fiery Islet",
         "Staunch Crewmate", "Corsair Captain", "Dire Fleet Captain", "Metallic Mimic"},
        {"Mountain"});
    CHECK(First(s, Rank(s)) == "Mountain");
}

TEST_CASE("Pirates discard: the ranking names EVERY hand card (no max-MV fall-through)")
{
    const GameState s = MakeState(
        {"Lightning Bolt", "Malcolm, the Eyes", "Aether Vial", "Unclaimed Territory",
         "Goblin Tomb Raider", "Forerunner of the Coalition", "Adaptive Automaton", "Kitesail Larcenist",
         "Siren Stormtamer"},
        {"Mountain", "Mountain"});
    const std::vector<int> order = Rank(s);
    CHECK(order.size() == s.players[0].hand.size());
}

TEST_CASE("Pirates ETB dig: same-name copies fold onto the first; index 0 is the base rule's pick")
{
    // Pirates 5d sweep gi14: top four = Aether Vial, Aether Vial, Siren Stormtamer, Goblin Tomb
    // Raider. The base list (all four) put the Raider at rank 3, outside the axis's 3-slot window.
    std::vector<Card> examined;
    for (const char* n : {"Aether Vial", "Aether Vial", "Siren Stormtamer", "Goblin Tomb Raider"})
    {
        Card c;
        c.m_name = n;
        c.RehashName();
        c.m_number = static_cast<int>(examined.size()) + 1;
        examined.push_back(c);
    }
    const std::vector<int> legal = {0, 1, 2, 3};
    GameState s;
    const std::vector<int> ranked = PiratesProvider().EtbDigCandidates(s, 0, examined, legal);
    CHECK(ranked == std::vector<int>{0, 2, 3});
    CHECK(PiratesProvider().EtbDigSearchWidth() >= 4);   // every distinct name of a 4-card look
    // The base provider is untouched (other decks: Acclaimed Contender keeps look order + copies).
    CHECK(GenericProvider().EtbDigCandidates(s, 0, examined, legal) == legal);
    CHECK(GenericProvider().EtbDigSearchWidth() == 0);
}

// ---- USER review of the discard policy (2026-09-27) ----------------------------------------------

TEST_CASE("Pirates discard: with 1 land total and no Vial out, the 3-drops go first (cannot be played)")
{
    GameState s = MakeState({"Corsair Captain", "Adaptive Automaton", "Goblin Tomb Raider", "Siren Stormtamer",
                             "Daring Buccaneer", "Staunch Crewmate", "Metallic Mimic", "Forerunner of the Coalition"},
                            {"Spirebluff Canal"});
    const std::vector<int> r = Rank(s);
    const int corsair = ShedPos(s, r, "Corsair Captain"), autom = ShedPos(s, r, "Adaptive Automaton"),
              fore = ShedPos(s, r, "Forerunner of the Coalition");
    for (const char* cheap : {"Goblin Tomb Raider", "Siren Stormtamer", "Daring Buccaneer"})
    {
        CAPTURE(cheap);
        CHECK(corsair < ShedPos(s, r, cheap));
        CHECK(autom   < ShedPos(s, r, cheap));
        CHECK(fore    < ShedPos(s, r, cheap));
    }
}

TEST_CASE("Pirates discard: 1 land out with lands 2 and 3 in hand keeps a 2-drop and a 3-drop by value")
{
    GameState s = MakeState({"Island", "Mountain", "Corsair Captain", "Metallic Mimic", "Siren Stormtamer",
                             "Goblin Tomb Raider", "Staunch Crewmate", "Adaptive Automaton"},
                            {"Spirebluff Canal"});
    const std::vector<int> r = Rank(s);
    // The curve (2 mana next turn, then 3, 3) is Mimic -> Corsair -> Automaton; the off-plan cards go
    // first, the kept lands last.
    CHECK(First(s, r) == "Siren Stormtamer");
    for (const char* off : {"Siren Stormtamer", "Goblin Tomb Raider", "Staunch Crewmate"})
    {
        CAPTURE(off);
        CHECK(ShedPos(s, r, off) < ShedPos(s, r, "Metallic Mimic"));
        CHECK(ShedPos(s, r, off) < ShedPos(s, r, "Corsair Captain"));
    }
    CHECK(ShedPos(s, r, "Corsair Captain") < ShedPos(s, r, "Island"));
    CHECK(ShedPos(s, r, "Corsair Captain") < ShedPos(s, r, "Mountain"));
}

TEST_CASE("Pirates discard: Metallic Mimic ranks below Dire Fleet Captain except on turn 1")
{
    for (int turn : {1, 5})
    {
        CAPTURE(turn);
        GameState s = MakeState({"Metallic Mimic", "Dire Fleet Captain", "Mountain", "Mountain", "Spirebluff Canal",
                                 "Fiery Islet", "Mountain", "Mountain"},
                                {"Blackcleave Cliffs"});
        s.turn_number = turn;
        const std::vector<int> r = Rank(s);
        const bool mimic_first = ShedPos(s, r, "Metallic Mimic") < ShedPos(s, r, "Dire Fleet Captain");
        CHECK(mimic_first == (turn != 1));
    }
}

TEST_CASE("Pirates discard: the Vial is a turn-1 keep -- shed at end of turn 2 unless mana-starved")
{
    // Turn 2, 2 lands out + 1 in hand = 3 reachable: the Vial is surplus and goes before any threat.
    GameState s = MakeState({"Aether Vial", "Mountain", "Siren Stormtamer", "Goblin Tomb Raider", "Daring Buccaneer",
                             "Staunch Crewmate", "Metallic Mimic", "Corsair Captain"},
                            {"Spirebluff Canal", "Unclaimed Territory"});
    s.turn_number = 2;
    std::vector<int> r = Rank(s);
    CHECK(First(s, r) == "Aether Vial");
    // Mana-starved (1 land out, none in hand): the Vial is kept over a weak threat.
    GameState t = MakeState({"Aether Vial", "Siren Stormtamer", "Goblin Tomb Raider", "Daring Buccaneer",
                             "Staunch Crewmate", "Metallic Mimic", "Corsair Captain", "Adaptive Automaton"},
                            {"Spirebluff Canal"});
    t.turn_number = 2;
    r = Rank(t);
    CHECK(ShedPos(t, r, "Siren Stormtamer") < ShedPos(t, r, "Aether Vial"));
}
