// Boars I5b (2026-11-13): rules-correctness fixes the Ilharg put exposed, plus Ilharg's dies clause and the general
// legend-rule keep choice.
//   (1) exalted's "attacks ALONE" counts the DECLARED attackers (CR 506.5 / 702.83), in both combats + the projection;
//   (2) every per-attacker "whenever <this / a> creature attacks" helper takes its SOURCES from the DECLARED prefix of
//       the attacker list only (CR 508.4: a creature put onto the battlefield attacking never attacked); recipients of
//       the effect and the player-level "whenever you attack" (Adeline) are unchanged;
//   (3) Ilharg's dies clause (dies_to_library_position): the searched "may" in both worlds + the viewer yes/no;
#include <doctest/doctest.h>
#include "ai/AIEngine.h"
#include "ai/Combat.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
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

// ---- (3) Ilharg's death clause: "When Ilharg dies ..., you may put it into its owner's library third from the top"
// (CardParams::dies_to_library_position 3). The "may" is SEARCHED (Action::dies_tuck twins on the cast that kills it;
// Plan::attack_put_choice's tuck bit for an attack put's legend-rule loser), identical in both worlds.
namespace
{
Action ICast(const char* name, int dies_tuck, bool emerge = false, int victim = 0)
{
    Action a;
    a.kind                 = Action::Kind::CastFromHand;
    a.card_name            = name;
    a.def                  = &IDef(name);
    a.hand_index           = -1;
    a.cost                 = IDef(name).card.m_mana_cost;
    a.card_mv              = IDef(name).card.m_mana_cost.ManaValue();
    a.emerge               = emerge;
    a.soulfire_own_targets = victim;
    a.dies_tuck            = static_cast<std::int8_t>(dies_tuck);
    return a;
}
TurnSolver::Plan IPlan(const std::vector<Action>& acts) { TurnSolver::Plan p; p.actions = acts; return p; }
bool InGraveyard(const GameState& s, int num)
{
    for (const Card& c : s.players[0].graveyard) { if (c.m_number == num) { return true; } }
    return false;
}
void IResolveStack(GameState& s)
{
    while (!s.stack.empty())
    {
        const StackEntry e = s.stack.back();
        s.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(s, e, *d); }
    }
}
// Ilharg #10 on the battlefield, a second Ilharg #20 in hand, five lands that pay {3}{R}{R}.
GameState ILegendBoard()
{
    GameState s = IBase();
    IPut(s, kIlharg, 10);
    for (int k = 0; k < 2; ++k) { IPut(s, "Mountain", 50 + k); }
    for (int k = 0; k < 3; ++k) { IPut(s, "Forest", 52 + k); }
    s.players[0].hand.push_back(ICard(kIlharg, 20));
    return s;
}
// Ilharg #10 on the battlefield, Decimator #60 in hand, four Forests pay the emerge bill {1}{G}{G}{G} (10 - MV 5).
GameState IEmergeBoard()
{
    GameState s = IBase();
    IPut(s, kIlharg, 10);
    for (int k = 0; k < 4; ++k) { IPut(s, "Forest", 50 + k); }
    s.players[0].hand.push_back(ICard("Decimator of the Provinces", 60));
    return s;
}
// The library of IBase is ten Forests 900..909: a tuck puts the dead card at index 2.
void CheckTucked(const GameState& s, int num)
{
    REQUIRE(s.players[0].library.size() == 11u);
    CHECK(s.players[0].library[2].m_number == num);
    CHECK(s.players[0].library[0].m_number == 900);
    CHECK(s.players[0].library[1].m_number == 901);
    CHECK(s.players[0].library[3].m_number == 902);
    CHECK_FALSE(InGraveyard(s, num));
}
void CheckLeft(const GameState& s, int num)
{
    CHECK(s.players[0].library.size() == 10u);
    CHECK(InGraveyard(s, num));
}
}   // namespace

TEST_CASE("I5b dies clause: card data -- Ilharg carries dies_to_library_position 3; the Horn audit still holds")
{
    const CardDefinition& d = IDef(kIlharg);
    CHECK(d.params.dies_to_library_position == 3);
    CHECK(d.hand_gain_audited);
}

TEST_CASE("I5b dies clause: the legend-rule loser goes third from the top when chosen, stays when declined -- both worlds")
{
    for (int tuck : { 1, 2 })
    {
        CAPTURE(tuck);
        GameState roll = ILegendBoard();
        TurnSolver::ApplyPlan(roll, IPlan({ ICast(kIlharg, tuck) }), /*is_pre_combat=*/true);
        CHECK(IFind(roll, 10) != nullptr);   // keep-oldest: the new copy is the loser
        CHECK(IFind(roll, 20) == nullptr);
        if (tuck == 1) { CheckTucked(roll, 20); } else { CheckLeft(roll, 20); }

        GameState exec = ILegendBoard();
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        ManaPool avail = AvailableManaPool(exec);
        MtgTestSeam::CastWithDiesTuck(eng, exec, exec.players[0].hand.back(), avail, tuck);
        REQUIRE_FALSE(exec.stack.empty());
        CHECK(exec.stack.front().dies_tuck == tuck);
        IResolveStack(exec);
        CHECK(exec.dies_tuck_pin == 0);   // the scope never leaks
        CHECK(IFind(exec, 10) != nullptr);
        CHECK(IFind(exec, 20) == nullptr);
        if (tuck == 1) { CheckTucked(exec, 20); } else { CheckLeft(exec, 20); }
        CHECK(roll.players[0].library.size() == exec.players[0].library.size());
    }
}

TEST_CASE("I5b dies clause: an emerge victim Ilharg is tucked at the CAST (before the spell resolves) -- both worlds")
{
    for (int tuck : { 1, 2 })
    {
        CAPTURE(tuck);
        GameState roll = IEmergeBoard();
        TurnSolver::ApplyPlan(roll, IPlan({ ICast("Decimator of the Provinces", tuck, /*emerge=*/true, 10) }), true);
        CHECK(IFind(roll, 60) != nullptr);
        CHECK(IFind(roll, 10) == nullptr);
        if (tuck == 1) { CheckTucked(roll, 10); } else { CheckLeft(roll, 10); }
        CHECK(roll.dies_tuck_pin == 0);

        GameState exec = IEmergeBoard();
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        ManaPool avail = AvailableManaPool(exec);
        MtgTestSeam::CastWithDiesTuck(eng, exec, exec.players[0].hand.back(), avail, tuck, /*emerge=*/true, 10);
        CHECK(IFind(exec, 10) == nullptr);   // sacrificed as the cost, already resolved its "may"
        if (tuck == 1) { CheckTucked(exec, 10); } else { CheckLeft(exec, 10); }
        CHECK(exec.dies_tuck_pin == 0);
        IResolveStack(exec);
        CHECK(IFind(exec, 60) != nullptr);
    }
}

TEST_CASE("I5b dies clause: a short library takes it at the bottom (CR: fewer than two cards)")
{
    for (int libn : { 0, 1, 2, 3 })
    {
        CAPTURE(libn);
        GameState s = IBase();
        while (static_cast<int>(s.players[0].library.size()) > libn)
        { s.players[0].library.erase(s.players[0].library.end() - 1); }
        s.players[0].graveyard.push_back(ICard(kIlharg, 20));
        DiesTuckPinScope pin(s, 1);
        ResolveDiesToLibraryPosition(s, 0, ICard(kIlharg, 20), 3);
        REQUIRE(s.players[0].library.size() == static_cast<std::size_t>(libn + 1));
        const std::size_t want = static_cast<std::size_t>(std::min(libn, 2));
        CHECK(s.players[0].library[want].m_number == 20);
        CHECK(s.players[0].graveyard.empty());
    }
}

TEST_CASE("I5b dies clause: unpinned (beyond the horizon) takes the provider default")
{
    {   // the legend-rule loser beside its surviving copy: leave it
        GameState roll = ILegendBoard();
        TurnSolver::ApplyPlan(roll, IPlan({ ICast(kIlharg, 0) }), true);
        CheckLeft(roll, 20);
    }
    {   // an emerge victim with no copy left: tuck it
        GameState roll = IEmergeBoard();
        TurnSolver::ApplyPlan(roll, IPlan({ ICast("Decimator of the Provinces", 0, true, 10) }), true);
        CheckTucked(roll, 10);
    }
    {   // a token Ilharg ceases to exist (CR 111.7): nothing goes to the library
        GameState s = IBase();
        const int il = IPut(s, kIlharg, 10);
        s.battlefield[static_cast<std::size_t>(il)].is_token = true;
        DiesTuckPinScope pin(s, 1);
        SacrificePermanentAt(s, 0, il);
        CHECK(s.players[0].library.size() == 10u);
    }
}

TEST_CASE("I5b dies clause: the enumeration twins every cast that kills an Ilharg, and only those")
{
    // SEARCHED play: autonomous d0 keeps the duplicate-legend prune (OfferDuplicateLegendCast's greedy gate), so a
    // second Ilharg is offered only where something weighs it -- DuplicateEntryOrDeathHasUpside counts the clause.
    struct SearchedScope
    {
        bool saved = g_searched_play;
        SearchedScope() { TurnSolver::SetSearchedPlay(true); }
        ~SearchedScope() { TurnSolver::SetSearchedPlay(saved); }
    } searched;
    auto tucks = [](const GameState& s, const char* name, bool emerge) {
        std::vector<int> out;
        for (const Action& a : TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/true))
        {
            if (a.kind == Action::Kind::CastFromHand && a.card_name == name && a.emerge == emerge)
            { out.push_back(a.dies_tuck); }
        }
        std::sort(out.begin(), out.end());
        return out;
    };
    {   // a legendary duplicate on the battlefield: tuck / leave, no untwinned cast
        const GameState s = ILegendBoard();
        CHECK(tucks(s, kIlharg, false) == std::vector<int>{ 1, 2 });
    }
    {   // the emerge cast whose victim is Ilharg: twinned
        const GameState s = IEmergeBoard();
        const std::vector<int> t = tucks(s, "Decimator of the Provinces", true);
        CHECK(std::count(t.begin(), t.end(), 1) == 1);
        CHECK(std::count(t.begin(), t.end(), 2) == 1);
    }
    {   // no Ilharg anywhere else: a lone Ilharg cast is not twinned
        GameState s = ILegendBoard();
        s.battlefield.erase(s.battlefield.begin());   // drop Ilharg #10
        CHECK(tucks(s, kIlharg, false) == std::vector<int>{ 0 });
    }
    {   // two copies in hand, none on the battlefield: the same-plan conditional twins 3 / 4
        GameState s = ILegendBoard();
        s.battlefield.erase(s.battlefield.begin());
        s.players[0].hand.push_back(ICard(kIlharg, 21));
        const std::vector<int> t = tucks(s, kIlharg, false);
        CHECK(std::count(t.begin(), t.end(), 3) >= 1);
        CHECK(std::count(t.begin(), t.end(), 3) == std::count(t.begin(), t.end(), 4));
        CHECK(std::count(t.begin(), t.end(), 0) == 0);
    }
}

TEST_CASE("I5b dies clause: an attack put of a second Ilharg carries the tuck answer in its pin")
{
    for (bool tuck : { true, false })
    {
        CAPTURE(tuck);
        GameState s = IBase();
        s.phase = Phase::Combat;
        const int il = IPut(s, kIlharg, 10);
        s.players[0].hand.push_back(ICard(kIlharg, 20));
        s.scripted_attack_put = static_cast<std::int16_t>(tuck ? (20 | kAttackPutTuckBit) : 20);
        std::vector<int> atk = { il };
        int declared_n = 1;
        FireAttackPutFromHand(s, 0, atk, declared_n);
        CHECK(IFind(s, 10) != nullptr);
        CHECK(IFind(s, 20) == nullptr);   // the put copy lost the legend rule
        CHECK_FALSE(InHand(s, 20));
        if (tuck) { CheckTucked(s, 20); } else { CheckLeft(s, 20); }
        CHECK(s.dies_tuck_pin == 0);
    }
    {   // the search fans BOTH answers for the Ilharg put
        GameState s = IBase();
        IPut(s, kIlharg, 10);
        s.players[0].hand.push_back(ICard(kIlharg, 20));
        TurnSolver::Plan base;
        base.land_decided = true;
        std::vector<int> c;
        for (const TurnSolver::Plan& p : TurnSolver::SubdecisionAxesForTest(s, /*is_pre_combat=*/true, { base }))
        { c.push_back(p.attack_put_choice); }
        CHECK(std::find(c.begin(), c.end(), 20) != c.end());
        CHECK(std::find(c.begin(), c.end(), 20 | kAttackPutTuckBit) != c.end());
        CHECK(std::find(c.begin(), c.end(), -2) != c.end());
    }
}

