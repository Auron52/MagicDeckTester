// The passive opponent's land drop follows each land's own entry rules (USER 2026-11-21: "the
// opponent can play lands without the triggers. So bouncelands don't bounce."): MDFC land backs,
// Karoo bounces, Lake of the Dead's replacement, enters-tapped conditions, ETB life gain, the legend
// rule, the drop-order preference, and 2HG head separation (the card-number band).
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/HeuristicDefaults.h"
#include "core/OpponentDeck.h"
#include "core/SpellEffects.h"
#include "runner/GoldFishRunner.h"

#include <string>
#include <vector>

namespace {

struct HeadsScope
{
    int saved = gamesetup::t_setup.opponent_heads;
    explicit HeadsScope(int h) { gamesetup::t_setup.opponent_heads = h; }
    ~HeadsScope() { gamesetup::t_setup.opponent_heads = saved; }
};

void LoadDb()
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

// A name-only opponent card, as Deal makes them.
Card Opp(const char* name, int number)
{
    REQUIRE(CardDatabase::Instance().Lookup(name) != nullptr);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

// An opponent land already on the battlefield (front block, arrival order).
void OnBoard(GameState& s, const char* name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    Permanent p;
    p.card = d->card;
    p.card.m_number = number;
    p.controller_index = 1;
    p.owner_index = 1;
    auto pos = s.battlefield.begin();
    while (pos != s.battlefield.end() && pos->controller_index == 1) { ++pos; }
    s.battlefield.insert(pos, p);
}

GameState Fresh(int heads = 1)
{
    LoadDb();
    HeadsScope h(heads);
    Decklist plain;
    const CardDefinition* forest = CardDatabase::Instance().Lookup("Forest");
    REQUIRE(forest != nullptr);
    for (int i = 0; i < 40; ++i) { plain.mainboard.push_back(forest->card); }
    GameState s = GoldFishRunner::SetupGame(plain, 7, 0);
    REQUIRE(s.opponent_plays_lands);
    s.players[1].hand.clear();
    s.players[1].graveyard.clear();
    s.opponent2_hand.clear();
    s.battlefield.clear();
    return s;
}

std::vector<std::string> OppBoard(const GameState& s)
{
    std::vector<std::string> out;
    for (const Permanent& p : s.battlefield) { if (p.controller_index == 1) { out.push_back(p.card.m_name.str()); } }
    return out;
}

const Permanent* Find(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

} // namespace

TEST_CASE("opponent MDFC spell//land: the land face is the drop, tapped (life declined), and a real land is preferred")
{
    struct Row { const char* front; const char* back; };
    for (const Row& r : { Row{"Turntimber Symbiosis", "Turntimber, Serpentine Wood"},
                          Row{"Sink into Stupor", "Soporific Springs"},
                          Row{"Legion Leadership", "Legion Stronghold"} })
    {
        GameState s = Fresh();
        const int life = s.players[1].life;
        s.players[1].hand.push_back(Opp(r.front, 100001));
        ResolvePassiveOpponentLandTurn(s);
        const Permanent* p = Find(s, 100001);
        REQUIRE(p != nullptr);
        CHECK(p->card.m_name == r.back);
        CHECK(p->card.IsLand());
        CHECK(p->tapped);
        CHECK(s.players[1].life == life);   // the passive opponent never pays the 3 life
        CHECK(s.players[1].hand.empty());
    }

    // A real land first: the MDFC stays in hand as the spell it also is.
    GameState s = Fresh();
    s.players[1].hand.push_back(Opp("Turntimber Symbiosis", 100001));
    s.players[1].hand.push_back(Opp("Forest", 100002));
    ResolvePassiveOpponentLandTurn(s);
    CHECK(OppBoard(s) == std::vector<std::string>{"Forest"});
    REQUIRE(s.players[1].hand.size() == 1u);
    CHECK(s.players[1].hand[0].m_name == "Turntimber Symbiosis");

    // A bounced land face returns to hand as its FRONT (CR 712.8a).
    GameState b = Fresh();
    OnBoard(b, "Turntimber, Serpentine Wood", 100005);
    b.players[1].hand.push_back(Opp("Gruul Turf", 100006));
    ResolvePassiveOpponentLandTurn(b);
    REQUIRE(b.players[1].hand.size() == 1u);
    CHECK(b.players[1].hand[0].m_name == "Turntimber Symbiosis");
    CHECK(b.players[1].hand[0].m_number == 100005);
}

TEST_CASE("opponent bounceland: enters tapped and returns the oldest other non-Karoo land, else another Karoo, else itself")
{
    // Alone: it returns itself, so the opponent's land count does not grow.
    {
        GameState s = Fresh();
        s.players[1].hand.push_back(Opp("Gruul Turf", 100001));
        ResolvePassiveOpponentLandTurn(s);
        CHECK(OppBoard(s).empty());
        REQUIRE(s.players[1].hand.size() == 1u);
        CHECK(s.players[1].hand[0].m_name == "Gruul Turf");
    }
    // With others: the oldest land that is not a Karoo goes to the right end of the hand.
    {
        GameState s = Fresh();
        OnBoard(s, "Gruul Turf", 100010);
        OnBoard(s, "Forest", 100011);
        OnBoard(s, "Mountain", 100012);
        s.players[1].hand.push_back(Opp("Island", 100020));
        s.players[1].hand.insert(s.players[1].hand.begin(), Opp("Izzet Boilerworks", 100013));
        ResolvePassiveOpponentLandTurn(s);
        CHECK(OppBoard(s) == std::vector<std::string>{"Gruul Turf", "Mountain", "Izzet Boilerworks"});
        const Permanent* k = Find(s, 100013);
        REQUIRE(k != nullptr);
        CHECK(k->tapped);
        REQUIRE(s.players[1].hand.size() == 2u);
        CHECK(s.players[1].hand[0].m_name == "Island");
        CHECK(s.players[1].hand[1].m_name == "Forest");
        CHECK(s.players[1].hand[1].m_number == 100011);
    }
    // Only another Karoo to return: that one goes back.
    {
        GameState s = Fresh();
        OnBoard(s, "Azorius Chancery", 100010);
        s.players[1].hand.push_back(Opp("Orzhov Basilica", 100011));
        ResolvePassiveOpponentLandTurn(s);
        CHECK(OppBoard(s) == std::vector<std::string>{"Orzhov Basilica"});
        REQUIRE(s.players[1].hand.size() == 1u);
        CHECK(s.players[1].hand[0].m_name == "Azorius Chancery");
    }
}

TEST_CASE("opponent Lake of the Dead sacrifices its oldest Swamp, else goes to the graveyard")
{
    {
        GameState s = Fresh();
        OnBoard(s, "Forest", 100010);
        OnBoard(s, "Swamp", 100011);
        OnBoard(s, "Swamp", 100012);
        s.players[1].hand.push_back(Opp("Lake of the Dead", 100013));
        ResolvePassiveOpponentLandTurn(s);
        CHECK(OppBoard(s) == std::vector<std::string>{"Forest", "Swamp", "Lake of the Dead"});
        CHECK(Find(s, 100011) == nullptr);
        REQUIRE(Find(s, 100013) != nullptr);
        CHECK_FALSE(Find(s, 100013)->tapped);
        REQUIRE(s.players[1].graveyard.size() == 1u);
        CHECK(s.players[1].graveyard[0].m_number == 100011);
    }
    {
        GameState s = Fresh();
        OnBoard(s, "Forest", 100010);
        s.players[1].hand.push_back(Opp("Lake of the Dead", 100013));
        ResolvePassiveOpponentLandTurn(s);
        CHECK(OppBoard(s) == std::vector<std::string>{"Forest"});
        CHECK(s.players[1].hand.empty());
        REQUIRE(s.players[1].graveyard.size() == 1u);
        CHECK(s.players[1].graveyard[0].m_name == "Lake of the Dead");
    }
}

TEST_CASE("opponent drop order: a land whose entry would be wasted waits while another land is available (provisional)")
{
    // A Karoo with nothing to return: the Forest goes first.
    {
        GameState s = Fresh();
        s.players[1].hand.push_back(Opp("Gruul Turf", 100001));
        s.players[1].hand.push_back(Opp("Forest", 100002));
        ResolvePassiveOpponentLandTurn(s);
        CHECK(OppBoard(s) == std::vector<std::string>{"Forest"});
        REQUIRE(s.players[1].hand.size() == 1u);
        CHECK(s.players[1].hand[0].m_name == "Gruul Turf");
        // Next turn the Karoo has a land to return.
        ResolvePassiveOpponentLandTurn(s);
        CHECK(OppBoard(s) == std::vector<std::string>{"Gruul Turf"});
        REQUIRE(s.players[1].hand.size() == 1u);
        CHECK(s.players[1].hand[0].m_name == "Forest");
    }
    // A Lake with no Swamp: the Island goes first; an MDFC back beats a wasted Lake too.
    {
        GameState s = Fresh();
        OnBoard(s, "Forest", 100010);
        s.players[1].hand.push_back(Opp("Lake of the Dead", 100001));
        s.players[1].hand.push_back(Opp("Island", 100002));
        ResolvePassiveOpponentLandTurn(s);
        CHECK(OppBoard(s) == std::vector<std::string>{"Forest", "Island"});
        CHECK(s.players[1].graveyard.empty());
    }
    {
        GameState s = Fresh();
        s.players[1].hand.push_back(Opp("Lake of the Dead", 100001));
        s.players[1].hand.push_back(Opp("Turntimber Symbiosis", 100002));
        ResolvePassiveOpponentLandTurn(s);
        CHECK(OppBoard(s) == std::vector<std::string>{"Turntimber, Serpentine Wood"});
    }
    // A second copy of a legendary land waits; played alone, the legend rule takes it.
    {
        GameState s = Fresh();
        OnBoard(s, "The Grey Havens", 100010);
        s.players[1].hand.push_back(Opp("The Grey Havens", 100001));
        s.players[1].hand.push_back(Opp("Island", 100002));
        ResolvePassiveOpponentLandTurn(s);
        CHECK(OppBoard(s) == std::vector<std::string>{"The Grey Havens", "Island"});
        ResolvePassiveOpponentLandTurn(s);
        CHECK(OppBoard(s) == std::vector<std::string>{"The Grey Havens", "Island"});
        REQUIRE(Find(s, 100010) != nullptr);
        REQUIRE(s.players[1].graveyard.size() == 1u);
        CHECK(s.players[1].graveyard[0].m_number == 100001);
    }
}

TEST_CASE("opponent enters-tapped conditions and ETB life gain are judged for that head")
{
    // Fastland: untapped with two or fewer other lands, tapped with three.
    {
        GameState s = Fresh();
        OnBoard(s, "Swamp", 100010);
        OnBoard(s, "Mountain", 100011);
        s.players[1].hand.push_back(Opp("Blackcleave Cliffs", 100001));
        ResolvePassiveOpponentLandTurn(s);
        CHECK_FALSE(Find(s, 100001)->tapped);
        s.players[1].hand.push_back(Opp("Blackcleave Cliffs", 100002));
        ResolvePassiveOpponentLandTurn(s);
        CHECK(Find(s, 100002)->tapped);
    }
    // Checkland: untapped only with a Mountain or Forest.
    {
        GameState s = Fresh();
        s.players[1].hand.push_back(Opp("Rootbound Crag", 100001));
        ResolvePassiveOpponentLandTurn(s);
        CHECK(Find(s, 100001)->tapped);
        OnBoard(s, "Forest", 100010);
        s.players[1].hand.push_back(Opp("Rootbound Crag", 100002));
        ResolvePassiveOpponentLandTurn(s);
        CHECK_FALSE(Find(s, 100002)->tapped);
    }
    // Reveal land: untapped when the hand holds a Mountain or Plains to reveal.
    {
        GameState s = Fresh();
        s.players[1].hand.push_back(Opp("Furycalm Snarl", 100001));
        ResolvePassiveOpponentLandTurn(s);
        CHECK(Find(s, 100001)->tapped);
        s.players[1].hand.push_back(Opp("Furycalm Snarl", 100002));
        s.players[1].hand.push_back(Opp("Plains", 100003));
        ResolvePassiveOpponentLandTurn(s);
        CHECK_FALSE(Find(s, 100002)->tapped);
    }
    // Radiant Fountain: the team gains 2.
    {
        GameState s = Fresh();
        const int life = s.players[1].life;
        s.players[1].hand.push_back(Opp("Radiant Fountain", 100001));
        ResolvePassiveOpponentLandTurn(s);
        CHECK(s.players[1].life == life + 2);
    }
}

TEST_CASE("2HG: each head's land entry reads and moves only that head's lands")
{
    // Head 2's Karoo with only head 1's Forest on the board returns itself, to head 2's hand.
    {
        GameState s = Fresh(2);
        REQUIRE(opponentdeck::HasSecondHead(s));
        OnBoard(s, "Forest", 100010);
        s.opponent2_hand.push_back(Opp("Gruul Turf", 110001));
        ResolvePassiveOpponentLandTurn(s);
        CHECK(OppBoard(s) == std::vector<std::string>{"Forest"});
        REQUIRE(s.opponent2_hand.size() == 1u);
        CHECK(s.opponent2_hand[0].m_name == "Gruul Turf");
        CHECK(s.players[1].hand.empty());
    }
    // With a land of its own it returns that one, never head 1's older Forest.
    {
        GameState s = Fresh(2);
        OnBoard(s, "Forest", 100010);
        OnBoard(s, "Mountain", 110010);
        s.opponent2_hand.push_back(Opp("Gruul Turf", 110001));
        ResolvePassiveOpponentLandTurn(s);
        CHECK(OppBoard(s) == std::vector<std::string>{"Forest", "Gruul Turf"});
        REQUIRE(s.opponent2_hand.size() == 1u);
        CHECK(s.opponent2_hand[0].m_number == 110010);
        CHECK(s.players[1].hand.empty());
    }
    // Head 2's Lake does not sacrifice head 1's Swamp; head 1's Karoo does not return head 2's land.
    {
        GameState s = Fresh(2);
        OnBoard(s, "Swamp", 100010);
        OnBoard(s, "Island", 110010);
        s.players[1].hand.push_back(Opp("Azorius Chancery", 100001));
        s.opponent2_hand.push_back(Opp("Lake of the Dead", 110001));
        ResolvePassiveOpponentLandTurn(s);
        CHECK(OppBoard(s) == std::vector<std::string>{"Island", "Azorius Chancery"});
        REQUIRE(s.players[1].hand.size() == 1u);
        CHECK(s.players[1].hand[0].m_name == "Swamp");
        CHECK(s.opponent2_hand.empty());
        CHECK(s.players[1].graveyard.empty());   // head 2's Lake went to its (untracked) graveyard
    }
    // A fastland counts its own head's lands only.
    {
        GameState s = Fresh(2);
        OnBoard(s, "Swamp", 100010);
        OnBoard(s, "Swamp", 100011);
        OnBoard(s, "Swamp", 100012);
        s.opponent2_hand.push_back(Opp("Blackcleave Cliffs", 110001));
        ResolvePassiveOpponentLandTurn(s);
        REQUIRE(Find(s, 110001) != nullptr);
        CHECK_FALSE(Find(s, 110001)->tapped);
    }
    // The legend rule is per player: one Boseiju per head survives.
    {
        GameState s = Fresh(2);
        OnBoard(s, "Boseiju, Who Endures", 100010);
        s.opponent2_hand.push_back(Opp("Boseiju, Who Endures", 110001));
        ResolvePassiveOpponentLandTurn(s);
        LegendRuleSbaCheckpoint(s);
        CHECK(OppBoard(s) == std::vector<std::string>{"Boseiju, Who Endures", "Boseiju, Who Endures"});
    }
}
