// Unit tests for Jorn, God of Winter: "Whenever Jorn attacks, untap each snow permanent you control."
// (attack_untap_snow_permanents: AttackUntapSnowFires at declare-attackers + UntapSnowPermanents after
// ResolveCombatDamage -- shared by GameEngine::CombatPhase and TurnSolver::SimulateCombat). Added 2026-10-03 when Snow switched from the Kaldring back face to
// the Jorn front face.
//
//  1. It FIRES AT ALL, and untaps every snow permanent we control: lands, a mana dork, an artifact,
//     and the attackers themselves (CR 506.4 -- they stay attacking).
//  2. "SNOW" and "YOU CONTROL" are both load-bearing: a tapped non-snow land of ours and a tapped
//     snow permanent of the opponent's stay tapped.
//  3. Jorn must ITSELF be attacking. Jorn on the battlefield while another creature attacks does
//     nothing -- without this the trigger would read as a static "untap on any attack".
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/GameSetup.h"
#include "core/SpellEffects.h"
#include "core/HeuristicDefaults.h"

#include <string>
#include <vector>

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

int PutTapped(GameState& s, const std::string& name, int controller, int number)
{
    Permanent p;
    p.card             = Def(name).card;
    p.card.m_number    = number;
    p.controller_index = controller;
    p.owner_index      = controller;
    p.tapped           = true;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

bool TappedByNumber(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return p.tapped; } }
    REQUIRE_MESSAGE(false, "permanent not found: ", number);
    return false;
}

GameState Fresh()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    return s;
}

}   // namespace

TEST_CASE("Jorn attacking untaps each snow permanent we control, and nothing else")
{
    EnsureCardsLoaded();
    REQUIRE(Def("Jorn, God of Winter").params.attack_untap_snow_permanents);

    GameState s = Fresh();
    const int jorn   = PutTapped(s, "Jorn, God of Winter",   0, 1);   // tapped by attacking
    const int treef  = PutTapped(s, "Abominable Treefolk",   0, 2);   // the other attacker
    PutTapped(s, "Snow-Covered Forest",   0, 3);
    PutTapped(s, "Boreal Druid",          0, 4);
    PutTapped(s, "Arcum's Astrolabe",     0, 5);
    PutTapped(s, "Forest",                0, 6);   // ours, NOT snow
    PutTapped(s, "Snow-Covered Island",   1, 7);   // snow, NOT ours

    REQUIRE(AttackUntapSnowFires(s, 0, { jorn, treef }));
    UntapSnowPermanents(s, 0);

    CHECK_FALSE(TappedByNumber(s, 1));   // Jorn itself
    CHECK_FALSE(TappedByNumber(s, 2));   // the other attacker
    CHECK_FALSE(TappedByNumber(s, 3));
    CHECK_FALSE(TappedByNumber(s, 4));
    CHECK_FALSE(TappedByNumber(s, 5));
    CHECK(TappedByNumber(s, 6));         // non-snow stays tapped
    CHECK(TappedByNumber(s, 7));         // the opponent's stays tapped
}

TEST_CASE("Jorn must itself be attacking")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    PutTapped(s, "Jorn, God of Winter", 0, 1);
    const int treef = PutTapped(s, "Abominable Treefolk", 0, 2);
    PutTapped(s, "Snow-Covered Forest", 0, 3);

    CHECK_FALSE(AttackUntapSnowFires(s, 0, { treef }));

    CHECK(TappedByNumber(s, 2));
    CHECK(TappedByNumber(s, 3));
}

// UntapSecondMainLive: main 2 is played on exactly the turns Jorn's untap fired (USER 2026-10-03,
// "We should not do the second main if Jorn did not fire"). Read from the post-combat board by the
// executor and every simulated turn alike, so these four cases pin the whole contract.
TEST_CASE("Second main is live only when our Jorn could attack this turn")
{
    EnsureCardsLoaded();
    {
        GameState s = Fresh();
        PutTapped(s, "Snow-Covered Forest", 0, 3);
        CHECK_FALSE(UntapSecondMainLive(s));                      // no Jorn
    }
    {
        GameState s = Fresh();
        const int j = PutTapped(s, "Jorn, God of Winter", 0, 1);
        s.battlefield[j].tapped = false;                          // it untapped itself attacking
        CHECK(UntapSecondMainLive(s));                            // attacked -> fired
        s.battlefield[j].entered_this_turn = true;
        CHECK_FALSE(UntapSecondMainLive(s));                      // cast this turn -> sick, no attack
    }
    {
        GameState s = Fresh();
        const int j = PutTapped(s, "Jorn, God of Winter", 1, 1);  // the OPPONENT's Jorn
        s.battlefield[j].tapped = false;
        CHECK_FALSE(UntapSecondMainLive(s));
    }
}
