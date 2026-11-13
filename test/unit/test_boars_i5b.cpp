// Boars I5b (2026-11-13): rules-correctness fixes the Ilharg put exposed, plus Ilharg's dies clause and the general
// legend-rule keep choice.
//   (1) exalted's "attacks ALONE" counts the DECLARED attackers (CR 506.5 / 702.83), in both combats + the projection;
//   (2) every per-attacker "whenever <this / a> creature attacks" helper takes its SOURCES from the DECLARED prefix of
//       the attacker list only (CR 508.4: a creature put onto the battlefield attacking never attacked); recipients of
//       the effect and the player-level "whenever you attack" (Adeline) are unchanged;
#include <doctest/doctest.h>
#include "ai/AIEngine.h"
#include "ai/Combat.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameEngine.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"
#include <algorithm>
#include <string>
#include <vector>

namespace
{
constexpr const char* kIlharg = "Ilharg, the Raze-Boar";
const CardDefinition& IDef(const char* name)
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, name);
    return *d;
}
Card ICard(const char* name, int number)
{
    Card c = IDef(name).card;
    c.m_number = number;
    return c;
}
int IPut(GameState& s, const char* name, int number, int controller = 0, bool sick = false)
{
    Permanent p;
    p.card = ICard(name, number);
    p.controller_index = p.owner_index = controller;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}
GameState IBase()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(ICard("Forest", 900 + k)); }
    return s;
}
const Permanent* IFind(const GameState& s, int num)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == num) { return &p; } }
    return nullptr;
}
bool InHand(const GameState& s, int num)
{
    for (const Card& c : s.players[0].hand) { if (c.m_number == num) { return true; } }
    return false;
}
}   // namespace

TEST_CASE("I5b exalted: the lone DECLARED attacker keeps its bonus when an undeclared body joins it (CR 506.5)")
{
    GameState s = IBase();
    s.phase = Phase::Combat;
    const int il = IPut(s, kIlharg, 10);
    IPut(s, "Ignoble Hierarch", 11);
    s.players[0].hand.push_back(ICard("Ignoble Hierarch", 20));   // a put Hierarch: its exalted never triggered
    std::vector<int> atk = { il };
    int declared_n = 1;
    const int at_declare = ExaltedAtDeclare(s, 0, declared_n);
    CHECK(at_declare == 1);
    s.scripted_attack_put = 20;
    FireAttackPutFromHand(s, 0, atk, declared_n);
    REQUIRE(atk.size() == 2u);
    CHECK(CountExalted(s.battlefield, 0) == 2);           // the late count would have read 2
    const int bonus = ExaltedBonusForCombat(s, atk, declared_n, at_declare);
    CHECK(bonus == 0);                                       // applied as a temp +1/+1 instead
    CHECK(IFind(s, 10)->temp_power_bonus == 1);
    CHECK(IFind(s, 10)->temp_tough_bonus == 1);
    CHECK(IFind(s, 20)->temp_power_bonus == 0);              // the put body does not get it
    ResolveCombatDamage(s, atk, bonus, /*collect_descs=*/false);
    CHECK(s.players[1].life == 20 - 7 - 0);                  // Ilharg 6+1, put Hierarch 0
}

TEST_CASE("I5b exalted: the historical paths are unchanged (alone -> returned; two declared -> none; gone -> none)")
{
    GameState s = IBase();
    const int il = IPut(s, kIlharg, 10);
    IPut(s, "Ignoble Hierarch", 11);
    std::vector<int> one = { il };
    CHECK(ExaltedBonusForCombat(s, one, 1, ExaltedAtDeclare(s, 0, 1)) == 1);
    CHECK(IFind(s, 10)->temp_power_bonus == 0);
    std::vector<int> two = { il, 1 };
    CHECK(ExaltedAtDeclare(s, 0, 2) == 0);
    CHECK(ExaltedBonusForCombat(s, two, 2, ExaltedAtDeclare(s, 0, 2)) == 0);
    // declared alone, then left the battlefield (declared prefix 0) while an undeclared body remains: no bonus.
    std::vector<int> other = { 1 };
    CHECK(ExaltedBonusForCombat(s, other, 0, 1) == 0);
    CHECK(s.battlefield[1].temp_power_bonus == 0);
}

TEST_CASE("I5b exalted: executor end to end and the projection credit exalted beside a put body")
{
    GameState ex = IBase();
    IPut(ex, kIlharg, 10);
    IPut(ex, "Ignoble Hierarch", 11, 0, /*sick=*/true);   // sick: stays home; its exalted still counts
    ex.players[0].hand.push_back(ICard("End-Raze Forerunners", 20));
    ex.scripted_attack_put = 20;
    GameState pr = ex;
    pr.scripted_attack_put = 20;
    // Ilharg 6 + End-Raze +2 + exalted 1, End-Raze 7 = 16.
    CHECK(TurnSolver::ProjectedAttackDamageForTest(pr) == 16);
    ex.phase = Phase::Combat;
    AIEngine eng(MulliganProfile::DefaultProfile(), 0, 0);
    GameEngine ge(eng);
    ge.PlayOutFrom(ex, ex.turn_number, GameEngine::ResumeAt::Combat);
    CHECK(ex.players[1].life == 20 - 16);
}

// ---- (2) per-attacker attack triggers: SOURCES = the declared prefix (CR 508.4) ----------------------------------

TEST_CASE("I5b attack triggers: a creature-scoped token maker put/joined undeclared makes nothing; declared, it does")
{
    GameState s = IBase();
    s.phase = Phase::Combat;
    const int pal  = IPut(s, "Accorder Paladin", 10);
    const int hero = IPut(s, "Hero of Bladehold", 11);   // undeclared: joined the attack (put / token)
    std::vector<int> atk = { pal, hero };
    {
        GameState t = s;
        const int before = static_cast<int>(t.battlefield.size());
        FireAttackCreateTokens(t, 0, &atk, /*declared_n=*/1);
        CHECK(static_cast<int>(t.battlefield.size()) == before);   // "whenever THIS attacks" never fired
    }
    {
        GameState t = s;
        const int before = static_cast<int>(t.battlefield.size());
        FireAttackCreateTokens(t, 0, &atk, /*declared_n=*/2);
        CHECK(static_cast<int>(t.battlefield.size()) == before + 2);   // declared Hero: two Soldiers
    }
}

TEST_CASE("I5b attack triggers: player-level 'whenever you attack' (Adeline) still fires with an undeclared body")
{
    GameState s = IBase();
    s.phase = Phase::Combat;
    IPut(s, "Adeline, Resplendent Cathar", 12);           // not attacking: the trigger is the PLAYER's
    const int pal  = IPut(s, "Accorder Paladin", 10);
    const int hero = IPut(s, "Hero of Bladehold", 11);
    std::vector<int> atk = { pal, hero };
    const int before = static_cast<int>(s.battlefield.size());
    FireAttackCreateTokens(s, 0, &atk, /*declared_n=*/1);
    REQUIRE(static_cast<int>(s.battlefield.size()) == before + 1);   // Adeline's Human only (one opponent)
    CHECK(s.battlefield.back().tapped);
}

TEST_CASE("I5b attack triggers: battle cry and a self-pump fire only for DECLARED sources; recipients are everyone")
{
    GameState s = IBase();
    s.phase = Phase::Combat;
    const int pal1 = IPut(s, "Accorder Paladin", 10);     // declared
    const int pal2 = IPut(s, "Accorder Paladin", 11);     // undeclared (put onto the battlefield attacking)
    const int cac  = IPut(s, "Jumbo Cactuar", 12);        // undeclared
    std::vector<int> atk = { pal1, pal2, cac };
    {
        GameState t = s;
        ApplyAttackSelfPumps(t, 0, atk, /*declared_n=*/1);
        ApplyBattleCry(t, 0, atk, /*declared_n=*/1);
        CHECK(t.battlefield[static_cast<std::size_t>(pal1)].temp_power_bonus == 0);   // its own cry excludes itself
        CHECK(t.battlefield[static_cast<std::size_t>(pal2)].temp_power_bonus == 1);   // gets pal1's; its own never fired
        CHECK(t.battlefield[static_cast<std::size_t>(cac)].temp_power_bonus == 1);    // no +9999: it never attacked
    }
    {
        GameState t = s;   // all declared: the historical whole-list behaviour
        ApplyAttackSelfPumps(t, 0, atk, /*declared_n=*/3);
        ApplyBattleCry(t, 0, atk, /*declared_n=*/3);
        CHECK(t.battlefield[static_cast<std::size_t>(pal1)].temp_power_bonus == 1);
        CHECK(t.battlefield[static_cast<std::size_t>(pal2)].temp_power_bonus == 1);
        CHECK(t.battlefield[static_cast<std::size_t>(cac)].temp_power_bonus == 9999 + 2);
    }
}

TEST_CASE("I5b attack triggers: RecountDeclared keeps the declared prefix across a by-number rebuild")
{
    GameState s = IBase();
    const int a = IPut(s, "Accorder Paladin", 10);
    const int b = IPut(s, "Accorder Paladin", 11);
    const int c = IPut(s, "Hero of Bladehold", 12);
    const std::vector<int> atk = { a, b, c };
    const std::vector<int> nums = DeclaredNumbers(s, atk, 2);
    CHECK(nums == std::vector<int>{ 10, 11 });
    CHECK(RecountDeclared(s, std::vector<int>{ b, c }, nums) == 1);   // a declared body left; c stays undeclared
    CHECK(RecountDeclared(s, atk, nums) == 2);
    CHECK(DeclaredSpan(atk, -1) == 3);
    CHECK(DeclaredSpan(atk, 5) == 3);
}
