// Boars I5a (2026-11-13): Ilharg, the Raze-Boar -- "Whenever Ilharg attacks, you may put a creature card from your
// hand onto the battlefield tapped and attacking. Return that creature to your hand at the beginning of the next end
// step." (attack_put_creature_from_hand / attack_put_return_at_end_step): the shared FireAttackPutFromHand (both
// combats), Permanent::return_at_end_step + PerformEndStepDelayedReturns (both end steps), the searched
// Plan::attack_put_choice -> GameState::scripted_attack_put pin, the projection addend, the Herald's Horn take-fold
// bounded term, and the D6 main-2 clause (a put body is a legal emerge victim before the end step).
// The dies -> library clause is I5b's (not modelled yet).
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

TEST_CASE("Ilharg: card data, the Horn audit and the Boars routing param")
{
    const CardDefinition& d = IDef(kIlharg);
    CHECK(d.params.attack_put_creature_from_hand);
    CHECK(d.params.attack_put_return_at_end_step);
    CHECK(d.card.m_mana_cost.ManaValue() == 5);
    CHECK(d.card.m_power == 6);
    CHECK(d.card.m_toughness == 6);
    CHECK(d.card.HasSupertype(Supertype::Legendary));
    CHECK(d.card.HasKeyword(Keyword::Trample));
    CHECK(d.hand_gain_audited);   // audited together with HornTakeFoldHolds' bounded return term
}

TEST_CASE("Ilharg: the put enters tapped and attacking, its ETB pump lands before damage, it is not declared")
{
    GameState s = IBase();
    s.phase = Phase::Combat;
    const int il = IPut(s, kIlharg, 10);
    s.players[0].hand.push_back(ICard("End-Raze Forerunners", 20));
    s.players[0].hand.push_back(ICard("Forest", 21));
    std::vector<int> atk = { il };
    int declared_n = 1;
    FireAttackPutFromHand(s, 0, atk, declared_n);
    REQUIRE(atk.size() == 2u);
    CHECK(declared_n == 1);
    const Permanent* er = IFind(s, 20);
    REQUIRE(er != nullptr);
    CHECK(er->tapped);
    CHECK(er->return_at_end_step);
    CHECK_FALSE(InHand(s, 20));
    CHECK(InHand(s, 21));
    // CR 508.1f: Ilharg was tapped by the declaration BEFORE End-Raze's vigilance grant resolved.
    CHECK(IFind(s, 10)->tapped);
    CHECK(IFind(s, 10)->temp_vigilance);
    CHECK(IFind(s, 10)->temp_power_bonus == 2);
    ResolveCombatDamage(s, atk, /*exalted_bonus=*/0, /*collect_descs=*/false);
    CHECK(s.players[1].life == 20 - 8 - 7);   // Ilharg 6+2, End-Raze 7
}

TEST_CASE("Ilharg: only a DECLARED Ilharg triggers; decline and a pinned card number are honoured")
{
    {   // Ilharg in the attack list but not declared (a put Ilharg): no trigger.
        GameState s = IBase();
        IPut(s, "Flinthoof Boar", 9);
        const int il = IPut(s, kIlharg, 10);
        s.players[0].hand.push_back(ICard("End-Raze Forerunners", 20));
        std::vector<int> atk = { 0, il };
        int declared_n = 1;
        FireAttackPutFromHand(s, 0, atk, declared_n);
        CHECK(atk.size() == 2u);
        CHECK(InHand(s, 20));
    }
    {   // the searched decline
        GameState s = IBase();
        const int il = IPut(s, kIlharg, 10);
        s.players[0].hand.push_back(ICard("End-Raze Forerunners", 20));
        s.scripted_attack_put = -2;
        std::vector<int> atk = { il };
        int declared_n = 1;
        FireAttackPutFromHand(s, 0, atk, declared_n);
        CHECK(atk.size() == 1u);
        CHECK(InHand(s, 20));
        CHECK(s.scripted_attack_put == -1);   // consumed
    }
    {   // a pinned card number beats the default (End-Raze)
        GameState s = IBase();
        const int il = IPut(s, kIlharg, 10);
        s.players[0].hand.push_back(ICard("End-Raze Forerunners", 20));
        s.players[0].hand.push_back(ICard("Birds of Paradise", 21));
        s.scripted_attack_put = 21;
        std::vector<int> atk = { il };
        int declared_n = 1;
        FireAttackPutFromHand(s, 0, atk, declared_n);
        CHECK(InHand(s, 20));
        CHECK_FALSE(InHand(s, 21));
        REQUIRE(IFind(s, 21) != nullptr);
        CHECK(IFind(s, 21)->tapped);
    }
    {   // unpinned: the provider default is End-Raze (power + its team pump)
        GameState s = IBase();
        const int il = IPut(s, kIlharg, 10);
        s.players[0].hand.push_back(ICard("Birds of Paradise", 21));
        s.players[0].hand.push_back(ICard("Decimator of the Provinces", 22));
        s.players[0].hand.push_back(ICard("End-Raze Forerunners", 20));
        std::vector<int> atk = { il };
        int declared_n = 1;
        FireAttackPutFromHand(s, 0, atk, declared_n);
        CHECK_FALSE(InHand(s, 20));
        CHECK(InHand(s, 21));
        CHECK(InHand(s, 22));
    }
}

TEST_CASE("Ilharg: the put is NOT cast (no Herald counters, no Decimator pump) and is not a declared attacker")
{
    GameState s = IBase();
    const int herald = IPut(s, "Herald of Ilharg", 8);
    IPut(s, "Raggadragga, Goreguts Boss", 9);
    const int il = IPut(s, kIlharg, 10);
    s.players[0].hand.push_back(ICard("Decimator of the Provinces", 22));
    s.scripted_attack_put = 22;
    std::vector<int> atk = { herald, il };
    int declared_n = 2;
    FireAttackPutFromHand(s, 0, atk, declared_n);
    REQUIRE(IFind(s, 22) != nullptr);
    CHECK(TotalCountersOn(*IFind(s, 8)) == 0);
    CHECK(IFind(s, 10)->temp_power_bonus == 0);
    CHECK(s.players[1].life == 20);

    // A put Birds (a creature with a mana ability) is in the attack list but was never declared: Raggadragga's
    // "whenever a creature you control with a mana ability attacks, untap it" does not see it.
    GameState r = IBase();
    IPut(r, "Raggadragga, Goreguts Boss", 9);
    const int il2 = IPut(r, kIlharg, 10);
    r.players[0].hand.push_back(ICard("Birds of Paradise", 21));
    std::vector<int> atk2 = { il2 };
    int dn2 = 1;
    FireAttackPutFromHand(r, 0, atk2, dn2);
    REQUIRE(atk2.size() == 2u);
    CHECK(AttackUntapManaAttackers(r, 0, atk2, dn2).empty());
}

TEST_CASE("Ilharg: the put creature returns to hand at the beginning of the end step; equipment falls off")
{
    GameState s = IBase();
    const int il = IPut(s, kIlharg, 10);
    const int gr = IPut(s, "Lightning Greaves", 11);
    s.players[0].hand.push_back(ICard("End-Raze Forerunners", 20));
    std::vector<int> atk = { il };
    int declared_n = 1;
    FireAttackPutFromHand(s, 0, atk, declared_n);
    REQUIRE(IFind(s, 20) != nullptr);
    s.battlefield[static_cast<std::size_t>(gr)].equipped_to = 20;
    PerformEndStepDelayedReturns(s);
    CHECK(IFind(s, 20) == nullptr);
    CHECK(InHand(s, 20));
    CHECK(IFind(s, 11)->equipped_to == 0);
    REQUIRE(IFind(s, 10) != nullptr);   // Ilharg stays

    // A put body that already left (an emerge victim) is not found: nothing returns (CR 603.7c).
    GameState t = IBase();
    const int il3 = IPut(t, kIlharg, 10);
    t.players[0].hand.push_back(ICard("Flinthoof Boar", 23));
    std::vector<int> atk3 = { il3 };
    int dn3 = 1;
    FireAttackPutFromHand(t, 0, atk3, dn3);
    for (std::size_t i = 0; i < t.battlefield.size(); ++i)
    { if (t.battlefield[i].card.m_number == 23) { SacrificePermanentAt(t, 0, static_cast<int>(i)); break; } }
    PerformEndStepDelayedReturns(t);
    CHECK_FALSE(InHand(t, 23));
}

TEST_CASE("Ilharg: executor end to end -- the put's ETB pump lands before damage and the body returns at end step")
{
    // GameEngine's real combat + main 2 + end step + cleanup (PlayOutFrom resumed at Combat, this turn only).
    GameState ex = IBase();
    IPut(ex, kIlharg, 10);
    ex.players[0].hand.push_back(ICard("End-Raze Forerunners", 20));
    ex.scripted_attack_put = 20;
    ex.phase               = Phase::Combat;
    AIEngine eng(MulliganProfile::DefaultProfile(), 0, 0);
    GameEngine ge(eng);
    ge.PlayOutFrom(ex, ex.turn_number, GameEngine::ResumeAt::Combat);
    // Ilharg 6 + End-Raze's +2 (its ETB pump resolved BEFORE damage) + End-Raze 7 = 15.
    CHECK(ex.players[1].life == 5);
    CHECK(InHand(ex, 20));             // returned at the beginning of the end step
    CHECK(IFind(ex, 20) == nullptr);
    CHECK(IFind(ex, 10) != nullptr);   // Ilharg stays
    CHECK(ex.scripted_attack_put == -1);
}

TEST_CASE("Ilharg: the put axis emits an EXPLICIT variant for every name, the default's included")
{
    auto choices = [](const GameState& s) {
        TurnSolver::Plan base;
        base.land_decided = true;
        std::vector<int> out;
        for (const TurnSolver::Plan& p : TurnSolver::SubdecisionAxesForTest(s, /*is_pre_combat=*/true, { base }))
        { out.push_back(p.attack_put_choice); }
        return out;
    };
    auto has = [](const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); };
    GameState s = IBase();
    IPut(s, kIlharg, 10);
    s.players[0].hand.push_back(ICard("End-Raze Forerunners", 20));   // the provider default (plan time)
    s.players[0].hand.push_back(ICard("Birds of Paradise", 21));
    s.players[0].hand.push_back(ICard("Birds of Paradise", 22));      // same name as 21: folded
    const std::vector<int> c = choices(s);
    CHECK(has(c, -1));    // the unpinned base
    CHECK(has(c, -2));    // decline
    CHECK(has(c, 20));    // the default's put is branched explicitly (a combat-time default can drift)
    CHECK(has(c, 21));
    CHECK_FALSE(has(c, 22));
    // An inherited pin is the base's own put: no variant for that NAME (either copy), the rest still fan.
    GameState t = s;
    t.scripted_attack_put = 22;
    const std::vector<int> d = choices(t);
    CHECK_FALSE(has(d, 21));
    CHECK_FALSE(has(d, 22));
    CHECK(has(d, 20));
    CHECK(has(d, -2));
}

TEST_CASE("Ilharg: the attack projection prices the put body and End-Raze's pump")
{
    GameState s = IBase();
    IPut(s, kIlharg, 10);
    s.players[0].hand.push_back(ICard("End-Raze Forerunners", 20));
    CHECK(TurnSolver::ProjectedAttackDamageForTest(s) >= 15);
    GameState d = s;
    d.scripted_attack_put = -2;   // a pinned decline prices Ilharg alone
    CHECK(TurnSolver::ProjectedAttackDamageForTest(d) == 6);
    // the projection never changes the real state
    CHECK(InHand(s, 20));
    CHECK_FALSE(s.battlefield.front().tapped);
}

TEST_CASE("Ilharg: the search finds the put line against an inherited decline pin")
{
    GameState s = IBase();
    s.players[1].life = 15;
    IPut(s, kIlharg, 10);
    IPut(s, "Forest", 30);
    s.players[0].hand.push_back(ICard("End-Raze Forerunners", 20));
    s.players[0].hand.push_back(ICard("Birds of Paradise", 21));
    s.scripted_attack_put = -2;   // e.g. a re-solve inheriting a decline: the base plan declines
    const TurnSolver::Plan pick = TurnSolver::SolveWithLookahead(s, /*is_pre_combat=*/true, /*depth=*/2);
    CHECK(pick.attack_put_choice == 20);
}

TEST_CASE("Ilharg: Herald's Horn take fold counts the end-step return; D6 main 2 opens for an emerge victim")
{
    GameState s = IBase();
    IPut(s, kIlharg, 10);
    for (int k = 0; k < 4; ++k) { s.players[0].hand.push_back(ICard("Forest", 40 + k)); }
    CHECK(HornTakeFoldHolds(s, 0, 0));              // 4 + 2 + 1 (the return) = 7
    s.players[0].hand.push_back(ICard("Forest", 44));
    CHECK_FALSE(HornTakeFoldHolds(s, 0, 0));        // 5 + 2 + 1 = 8 > 7: searched

    GameState m = IBase();
    m.deck_has_attack_untap = true;
    m.phase = Phase::PostCombatMain;
    const int il = IPut(m, kIlharg, 10);
    m.players[0].hand.push_back(ICard("End-Raze Forerunners", 20));
    std::vector<int> atk = { il };
    int dn = 1;
    FireAttackPutFromHand(m, 0, atk, dn);
    for (Permanent& p : m.battlefield) { p.temp_vigilance = false; }   // isolate the fourth clause
    CHECK_FALSE(UntapSecondMainLive(m));          // no emerge card in hand
    m.players[0].hand.push_back(ICard("Decimator of the Provinces", 22));
    CHECK(UntapSecondMainLive(m));                // the put End-Raze is a legal emerge victim before it returns
}
