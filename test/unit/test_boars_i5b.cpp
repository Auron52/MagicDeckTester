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
#include "ai/LandPlay.h"
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
    {   // a legendary duplicate on the battlefield: tuck / leave, no untwinned cast (each also twinned on the
        // legend-rule keep since I5b item 4 -- two casts per answer). The old copy entered this turn, so the copies
        // differ in more than age and fold F-E1 (Blood Sun step 7) keeps both keep twins.
        GameState s = ILegendBoard();
        for (Permanent& p : s.battlefield) { if (p.card.m_name == kIlharg) { p.entered_this_turn = true; } }
        CHECK(tucks(s, kIlharg, false) == std::vector<int>{ 1, 1, 2, 2 });
    }
    {   // the old copy plain and un-sick: fold F-E1 (LegendKeepOldDominates) drops keep-newest -- same death event,
        // same tuck answer, and the old copy can attack now -- so one cast per tuck answer
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

// I4 lockstep (found in I5b item 3): the rollout's breakpoint-continuation cast RECORD must carry Action::emerge. Without
// it, the executor's replay_recorded handed CastRiders::Of a record with emerge == false and replayed Decimator as the
// HARD cast ({10}, unaffordable) -- no sacrifice, no Decimator: an fd-diverge. The victim rides soulfire_own_targets.
TEST_CASE("I5b emerge record: a breakpoint continuation's emerge cast replays as emerge, sacrificing the same victim")
{
    // Gitaxian Probe (Island) draws Decimator #60 off the top; the continuation emerges it off Ilharg #10 (4 Forests).
    GameState initial = IBase();
    IPut(initial, kIlharg, 10);
    IPut(initial, "Island", 49);
    for (int k = 0; k < 4; ++k) { IPut(initial, "Forest", 50 + k); }
    initial.players[0].library.insert(initial.players[0].library.begin(), ICard("Decimator of the Provinces", 60));
    initial.players[0].hand.push_back(ICard("Gitaxian Probe", 70));

    TurnSolver::Plan prefix = IPlan({ ICast("Gitaxian Probe", 0) });
    prefix.bp_choice = TurnSolver::kBpEmptyChoice;
    prefix.bp_all    = true;
    GameState reached = initial;
    TurnSolver::ApplyPlan(reached, prefix, true);
    REQUIRE(std::any_of(reached.players[0].hand.begin(), reached.players[0].hand.end(),
                        [](const Card& c) { return c.m_number == 60; }));
    const std::vector<TurnSolver::Plan> cands = TurnSolver::EnumerateBreakpointPlans(reached, true);
    const auto chosen = std::find_if(cands.begin(), cands.end(), [](const TurnSolver::Plan& p) {
        return p.actions.size() == 1 && p.actions[0].kind == Action::Kind::CastFromHand
            && p.actions[0].card_name.str() == "Decimator of the Provinces" && p.actions[0].emerge
            && p.actions[0].soulfire_own_targets == 10 && p.actions[0].dies_tuck == 1;
    });
    REQUIRE(chosen != cands.end());

    TurnSolver::Plan recorded = IPlan({ ICast("Gitaxian Probe", 0) });
    recorded.bp_at     = 0;
    recorded.bp_choice = static_cast<int>(chosen - cands.begin());
    GameState roll = initial;
    TurnSolver::ApplyPlanWithRecordsForTest(roll, recorded, true);
    // A main-level breakpoint's re-solve records its casts straight into Plan::breakpoint_actions (the
    // top-level sink); a nested one would nest under Action::breakpoint_casts. Either way the same record.
    const std::vector<Action>& recs = recorded.breakpoint_actions;
    const auto rec = std::find_if(recs.begin(), recs.end(), [](const Action& a) {
        return a.card_name.str() == "Decimator of the Provinces";
    });
    REQUIRE(rec != recs.end());
    CHECK(rec->emerge);
    CHECK(rec->soulfire_own_targets == 10);
    CHECK(rec->dies_tuck == 1);
    CHECK(IFind(roll, 60) != nullptr);
    CHECK(IFind(roll, 10) == nullptr);

    {   // the rollout tucked Ilharg third from the top of the post-draw library
        const auto& lib = roll.players[0].library;
        REQUIRE(lib.size() >= 3u);
        CHECK(lib[2].m_number == 10);
    }

    GameState exec = initial;
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, recorded, true);
    eng.TakeTurn(exec, true, IResolveStack);
    CHECK(IFind(exec, 60) != nullptr);   // Decimator resolved: the emerge cost was paid
    CHECK(IFind(exec, 10) == nullptr);   // ...by sacrificing the same victim
    REQUIRE(exec.players[0].library.size() == roll.players[0].library.size());
    for (std::size_t i = 0; i < roll.players[0].library.size(); ++i)
    { CHECK(exec.players[0].library[i].m_number == roll.players[0].library[i].m_number); }
    REQUIRE(exec.battlefield.size() == roll.battlefield.size());
    for (std::size_t i = 0; i < roll.battlefield.size(); ++i)
    {
        INFO("permanent ", roll.battlefield[i].card.m_number);
        CHECK(exec.battlefield[i].card.m_number == roll.battlefield[i].card.m_number);
        CHECK(exec.battlefield[i].tapped == roll.battlefield[i].tapped);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// I5b item 4: the legend-rule keep (CR 704.5j) is SEARCHED in both worlds and asked of a human for every legend.
// ---------------------------------------------------------------------------------------------------------------------
namespace
{
constexpr const char* kRagga = "Raggadragga, Goreguts Boss";
struct LkSearchedScope
{
    bool saved = g_searched_play;
    LkSearchedScope() { TurnSolver::SetSearchedPlay(true); }
    ~LkSearchedScope() { TurnSolver::SetSearchedPlay(saved); }
};
Action LkCast(const char* name, int legend_keep, int dies_tuck = 0)
{
    Action a = ICast(name, dies_tuck);
    a.legend_keep = static_cast<std::int8_t>(legend_keep);
    return a;
}
// Ilharg #10 (TAPPED: it differs from a fresh copy) on the battlefield, a second Ilharg #20 in hand, five lands.
GameState LkIlhargBoard()
{
    GameState s = ILegendBoard();
    s.battlefield[0].tapped = true;
    return s;
}
// A human chooser that records every legend-rule prompt and keeps option `pick`.
struct LkChooser
{
    int                      pick;
    std::vector<std::string> sources;
    BounceChooser            fn;
    BounceChooser*           saved;
    explicit LkChooser(int p) : pick(p), saved(g_play_sacrifice_chooser)
    {
        fn = [this](const GameState&, int, const std::string& source, const std::vector<int>&, int) -> int
        { sources.push_back(source); return pick; };
        g_play_sacrifice_chooser = &fn;
    }
    ~LkChooser() { g_play_sacrifice_chooser = saved; }
};
}   // namespace

TEST_CASE("I5b legend keep: the cast's rider decides which copy survives -- executor and rollout agree")
{
    for (int keep : { 1, 2 })
    {
        CAPTURE(keep);
        GameState roll = LkIlhargBoard();
        TurnSolver::ApplyPlan(roll, IPlan({ LkCast(kIlharg, keep, /*dies_tuck=*/2) }), /*is_pre_combat=*/true);
        GameState exec = LkIlhargBoard();
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        ManaPool avail = AvailableManaPool(exec);
        MtgTestSeam::CastWithDiesTuck(eng, exec, exec.players[0].hand.back(), avail, /*dies_tuck=*/2, false, 0, keep);
        REQUIRE_FALSE(exec.stack.empty());
        CHECK(exec.stack.front().legend_keep == keep);
        IResolveStack(exec);
        CHECK(g_legend_keep_pin.v == 0);   // the scope never leaks
        for (const GameState* s : { &roll, &exec })
        {
            const int survivor = keep == 1 ? 10 : 20, loser = keep == 1 ? 20 : 10;
            CHECK(IFind(*s, survivor) != nullptr);
            CHECK(IFind(*s, loser) == nullptr);
            CHECK(InGraveyard(*s, loser));   // left (dies_tuck 2): the death clause reads the loser either way
        }
        // keep-newest keeps the UNTAPPED fresh copy; keep-oldest the tapped one.
        CHECK(IFind(roll, keep == 1 ? 10 : 20)->tapped == (keep == 1));
        CHECK(IFind(exec, keep == 1 ? 10 : 20)->tapped == (keep == 1));
    }
}

TEST_CASE("I5b legend keep: differing copies -- both keep answers are reachable by the search")
{
    LkSearchedScope searched;
    auto keeps = [](const GameState& s, const char* name) {
        std::vector<int> out;
        for (const Action& a : TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/true))
        { if (a.kind == Action::Kind::CastFromHand && a.card_name == name) { out.push_back(a.legend_keep); } }
        std::sort(out.begin(), out.end());
        return out;
    };
    {   // Raggadragga on the battlefield, a second in hand: keep-oldest / keep-newest
        GameState s = ILegendBoard();
        s.battlefield.erase(s.battlefield.begin());
        s.players[0].hand.clear();
        IPut(s, kRagga, 30);
        s.players[0].hand.push_back(ICard(kRagga, 31));
        CHECK(keeps(s, kRagga) == std::vector<int>{ 1, 2 });
    }
    {   // Ilharg: the keep axis crosses the dies-clause axis (four casts)
        const GameState s = LkIlhargBoard();
        CHECK(keeps(s, kIlharg) == std::vector<int>{ 1, 1, 2, 2 });
    }
    {   // a lone legend (no same-name permanent): no keep twin
        GameState s = ILegendBoard();
        s.battlefield.erase(s.battlefield.begin());
        s.players[0].hand.clear();
        s.players[0].hand.push_back(ICard(kRagga, 31));
        CHECK(keeps(s, kRagga) == std::vector<int>{ 0 });
    }
    {   // two copies cast by ONE plan, none on the battlefield: the plan is fanned, both casts carry one answer
        GameState s = ILegendBoard();
        s.battlefield.erase(s.battlefield.begin());
        s.players[0].hand.clear();
        Action a = ICast(kRagga, 0), b = ICast(kRagga, 0);
        TurnSolver::Plan two = IPlan({ a, b }), one = IPlan({ a });
        two.land_decided = one.land_decided = true;
        int fan_old = 0, fan_new = 0, singles = 0;
        for (const TurnSolver::Plan& p : TurnSolver::SubdecisionAxesForTest(s, /*is_pre_combat=*/true, { two, one }))
        {
            if (p.actions.size() == 1) { ++singles; CHECK(p.actions[0].legend_keep == 0); continue; }
            REQUIRE(p.actions.size() == 2u);
            CHECK(p.actions[0].legend_keep == p.actions[1].legend_keep);
            if (p.actions[0].legend_keep == 1) { ++fan_old; }
            if (p.actions[0].legend_keep == 2) { ++fan_new; }
        }
        CHECK(fan_old == 1);
        CHECK(fan_new == 1);
        CHECK(singles == 1);
    }
    {   // autonomous d0 (not searched play): no twins -- the provider's keep is the d0 runner's policy
        TurnSolver::SetSearchedPlay(false);
        const GameState s = LkIlhargBoard();
        for (int k : keeps(s, kIlharg)) { CHECK(k == 0); }
        TurnSolver::SetSearchedPlay(true);
    }
}

TEST_CASE("I5b legend keep: identical copies fold; a differing non-Chandra legend asks the human, once")
{
    GameState s = IBase();
    IPut(s, kRagga, 30, 0, /*sick=*/true);
    IPut(s, kRagga, 31, 0, /*sick=*/true);
    CHECK(LegendCopiesInterchangeable(s, { 0, 1 }));
    {   // identical: no prompt, the provider's keep (oldest)
        LkChooser human(1);
        GameState t = s;
        EnforceLegendRule(t, 0);
        CHECK(human.sources.empty());
        CHECK(IFind(t, 30) != nullptr);
        CHECK(IFind(t, 31) == nullptr);
    }
    s.battlefield[0].tapped = true;
    CHECK_FALSE(LegendCopiesInterchangeable(s, { 0, 1 }));
    {   // differing: asked with the short name, and the human's keep (the newest) is applied
        LkChooser human(1);
        GameState t = s;
        EnforceLegendRule(t, 0);
        REQUIRE(human.sources.size() == 1u);
        CHECK(human.sources[0] == "Legend rule: choose Raggadragga to keep");
        CHECK(IFind(t, 31) != nullptr);
        CHECK(IFind(t, 30) == nullptr);
    }
    {   // the provisional loser asks FIRST (inside the cascade) and the SBA reuses that answer: one prompt, consistent
        LkChooser human(1);
        GameState t = s;
        CHECK(LegendRuleLoser(t.battlefield[0], t));         // #30 is the copy the human dropped
        CHECK_FALSE(LegendRuleLoser(t.battlefield[1], t));
        EnforceLegendRule(t, 0);
        CHECK(human.sources.size() == 1u);
        CHECK(IFind(t, 31) != nullptr);
        CHECK(IFind(t, 30) == nullptr);
        CHECK(g_legend_keep_asked.controller == -1);          // consumed by the SBA
    }
    {   // the searched pin decides when installed, and only for the legend it names
        GameState t = s;
        {
            LegendKeepPinScope pin(2, kRagga);
            CHECK(LegendRuleLoser(t.battlefield[0], t));
            EnforceLegendRule(t, 0);
        }
        CHECK(IFind(t, 31) != nullptr);
        GameState u = s;
        {
            LegendKeepPinScope pin(2, kIlharg);   // another name: the provider keeps the oldest
            EnforceLegendRule(u, 0);
        }
        CHECK(IFind(u, 30) != nullptr);
    }
}

TEST_CASE("I5b legend keep: Ilharg's attack put carries the keep answer in its pin (kAttackPutKeepNewBit)")
{
    for (bool keep_new : { false, true })
    {
        CAPTURE(keep_new);
        GameState s = IBase();
        s.phase = Phase::Combat;
        const int il = IPut(s, kIlharg, 10);
        s.players[0].hand.push_back(ICard(kIlharg, 20));
        s.scripted_attack_put = static_cast<std::int16_t>(20 | (keep_new ? kAttackPutKeepNewBit : 0));
        std::vector<int> atk = { il };
        int declared_n = 1;
        FireAttackPutFromHand(s, 0, atk, declared_n);
        const int survivor = keep_new ? 20 : 10, loser = keep_new ? 10 : 20;
        REQUIRE(IFind(s, survivor) != nullptr);
        CHECK(IFind(s, loser) == nullptr);
        CHECK(InGraveyard(s, loser));
        CHECK(atk.size() == 1u);   // exactly one Ilharg is still attacking
        CHECK(g_legend_keep_pin.v == 0);
        if (keep_new) { CHECK(IFind(s, 20)->return_at_end_step); }
    }
    {   // the search fans the keep-new twins beside the plain and tuck pins
        GameState s = IBase();
        IPut(s, kIlharg, 10);
        s.players[0].hand.push_back(ICard(kIlharg, 20));
        TurnSolver::Plan base;
        base.land_decided = true;
        std::vector<int> c;
        for (const TurnSolver::Plan& p : TurnSolver::SubdecisionAxesForTest(s, /*is_pre_combat=*/true, { base }))
        { c.push_back(p.attack_put_choice); }
        for (int want : { 20, 20 | kAttackPutTuckBit, 20 | kAttackPutKeepNewBit,
                          20 | kAttackPutKeepNewBit | kAttackPutTuckBit })
        { CHECK(std::count(c.begin(), c.end(), want) == 1); }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Boars LK (2026-11-13): the legend-rule keep on every route WITHOUT a cast rider is SEARCHED for the turn
// (Plan::legend_keep_turn -> GameState::scripted_legend_keep): legendary land drops, puts, nested free casts, token /
// copy entrants, reanimation.
// ---------------------------------------------------------------------------------------------------------------------
namespace
{
constexpr const char* kHavens = "The Grey Havens";
constexpr const char* kMuxus  = "Muxus, Goblin Grandee";
constexpr const char* kSurrak = "Surrak and Goreclaw";
std::vector<int> LkTurnKeeps(const GameState& s, const TurnSolver::Plan& base)
{
    std::vector<int> out;
    for (const TurnSolver::Plan& p : TurnSolver::SubdecisionAxesForTest(s, /*is_pre_combat=*/true, { base }))
    { out.push_back(p.legend_keep_turn); }
    // Other axes may fan the same plan (the Havens' etb_scry disposition, the Lackey pick): reduce to the SHAPE --
    // {1, 2} = every variant twinned (as many keep-old as keep-new, none unpinned), {0} = none twinned.
    const long n0 = std::count(out.begin(), out.end(), 0), n1 = std::count(out.begin(), out.end(), 1),
               n2 = std::count(out.begin(), out.end(), 2);
    if (n0 == 0 && n1 > 0 && n1 == n2) { return { 1, 2 }; }
    if (n1 == 0 && n2 == 0 && n0 > 0) { return { 0 }; }
    return out;
}
// The Grey Havens #10 on the battlefield (TAPPED for mana this turn), a second Havens #20 in hand, no land played.
GameState LkHavensBoard()
{
    GameState s = IBase();
    s.players[0].lands_played_this_turn = 0;
    IPut(s, kHavens, 10);
    s.battlefield.back().tapped = true;
    s.players[0].hand.push_back(ICard(kHavens, 20));
    return s;
}
TurnSolver::Plan LkLandPlan(const char* land)
{
    TurnSolver::Plan p;
    p.land_decided = true;
    p.land_to_play = land;
    return p;
}
}   // namespace

TEST_CASE("LK land: a legendary land drop beside a same-name land is twinned; a plain drop or a lone legend is not")
{
    LkSearchedScope searched;
    const GameState s = LkHavensBoard();
    CHECK(LkTurnKeeps(s, LkLandPlan(kHavens)) == std::vector<int>{ 1, 2 });
    CHECK(LkTurnKeeps(s, LkLandPlan("Forest")) == std::vector<int>{ 0 });
    {   // no Havens on the battlefield: the drop makes no group
        GameState t = s;
        t.battlefield.clear();
        CHECK(LkTurnKeeps(t, LkLandPlan(kHavens)) == std::vector<int>{ 0 });
    }
    {   // autonomous d0 (not searched play): the provider's keep is the d0 runner's policy
        TurnSolver::SetSearchedPlay(false);
        CHECK(LkTurnKeeps(s, LkLandPlan(kHavens)) == std::vector<int>{ 0 });
        TurnSolver::SetSearchedPlay(true);
    }
}

namespace {
// The executor's land drop: the shared chokepoint (LandPlay.cpp) its TryPlaySpecificLand reaches.
void LkExecDrop(GameState& exec, const TurnSolver::Plan& plan, int number)
{
    TurnSolver::WritePostApplyStatePins(exec, plan);   // before the plan's actions, as AIEngine::TakeTurn
    std::size_t hi = 0;
    while (exec.players[0].hand[hi].m_number != number) { ++hi; }
    REQUIRE(PlayLandFromHand(exec, hi, IDef(kHavens), LandPlayOptions{}));
}
}   // namespace

TEST_CASE("LK land: the turn pin decides the land drop's legend rule -- the rollout and the executor read it alike")
{
    for (int keep : { 0, 1, 2 })
    {
        CAPTURE(keep);
        TurnSolver::Plan plan = LkLandPlan(kHavens);
        plan.legend_keep_turn = static_cast<std::int8_t>(keep);
        GameState roll = LkHavensBoard();
        TurnSolver::ApplyPlan(roll, plan, /*is_pre_combat=*/true);
        CHECK(roll.scripted_legend_keep == keep);
        // CR 704.3: the duplicate is legend-ruled at the land play itself, before any combat SBA.
        GameState exec = LkHavensBoard();
        LkExecDrop(exec, plan, 20);
        const int survivor = keep == 2 ? 20 : 10, loser = keep == 2 ? 10 : 20;   // 0: the provider keeps the oldest
        for (const GameState* g : { &roll, &exec })
        {
            CHECK(IFind(*g, survivor) != nullptr);
            CHECK(IFind(*g, loser) == nullptr);
            CHECK(InGraveyard(*g, loser));
        }
        // keep-newest keeps an UNTAPPED Havens (a mana for main 2); keep-oldest the tapped one.
        if (keep != 0) { CHECK(IFind(roll, survivor)->tapped == (keep == 1)); }
    }
    {   // a name-scoped cast rider still wins for ITS legend; the turn pin covers the rest
        GameState s = LkHavensBoard();
        IPut(s, kHavens, 20);
        s.scripted_legend_keep = 2;
        {
            LegendKeepPinScope pin(1, kHavens);
            EnforceLegendRule(s, 0);
        }
        CHECK(IFind(s, 10) != nullptr);
        CHECK(IFind(s, 20) == nullptr);
    }
    {   // the turn pin is the ACTIVE player's: the opponent's legend rule is the provider's
        GameState s = IBase();
        IPut(s, kHavens, 10, /*controller=*/1);
        IPut(s, kHavens, 20, /*controller=*/1);
        s.scripted_legend_keep = 2;
        EnforceLegendRule(s, 1);
        CHECK(IFind(s, 10) != nullptr);
        CHECK(IFind(s, 20) == nullptr);
    }
}

TEST_CASE("LK put: Goblin Lackey's put of a second Muxus is twinned and reads the turn pin")
{
    LkSearchedScope searched;
    GameState s = IBase();
    IPut(s, "Goblin Lackey", 5);
    IPut(s, kMuxus, 10);
    s.battlefield.back().tapped = true;
    s.players[0].hand.push_back(ICard(kMuxus, 20));
    TurnSolver::Plan base;
    base.land_decided = true;
    CHECK(LkTurnKeeps(s, base) == std::vector<int>{ 1, 2 });
    {   // no Lackey (no non-cast route): the hand Muxus can only be CAST -- the cast's own twin (item 4) covers it
        GameState t = s;
        t.battlefield.erase(t.battlefield.begin());
        CHECK(LkTurnKeeps(t, base) == std::vector<int>{ 0 });
    }
    for (int keep : { 1, 2 })
    {
        CAPTURE(keep);
        GameState g = s;
        g.phase = Phase::Combat;
        g.scripted_legend_keep = static_cast<std::int8_t>(keep);
        FireCombatDamageCheatIntoPlay(g, 0, std::vector<int>{ 0 });
        const int survivor = keep == 1 ? 10 : 20, loser = keep == 1 ? 20 : 10;
        CHECK(IFind(g, survivor) != nullptr);
        CHECK(IFind(g, loser) == nullptr);
    }
}

TEST_CASE("LK put: a tutor-put (Green Sun's Zenith) that can find a second Surrak is twinned; none to find is not")
{
    LkSearchedScope searched;
    GameState s = IBase();
    IPut(s, kSurrak, 10);
    s.players[0].library.push_back(ICard(kSurrak, 20));
    s.players[0].hand.push_back(ICard("Green Sun's Zenith", 30));
    Action gsz;
    gsz.kind      = Action::Kind::CastFromHand;
    gsz.card_name = InternedName("Green Sun's Zenith");
    TurnSolver::Plan base = IPlan({ gsz });
    base.land_decided = true;
    CHECK(LkTurnKeeps(s, base) == std::vector<int>{ 1, 2 });
    TurnSolver::Plan none;
    none.land_decided = true;
    CHECK(LkTurnKeeps(s, none) == std::vector<int>{ 0 });   // nothing in the plan can put the library Surrak
    GameState t = s;
    { Library& lib = t.players[0].library; Library keep; for (std::size_t i = 0; i + 1 < lib.size(); ++i) { keep.push_back(lib[i]); } lib = keep; }
    CHECK(LkTurnKeeps(t, base) == std::vector<int>{ 0 });   // no second Surrak to find
}

TEST_CASE("LK copy: a token copy of our legend (Twinflame) is twinned")
{
    LkSearchedScope searched;
    GameState s = IBase();
    IPut(s, kSurrak, 10);
    Action tf;
    tf.kind      = Action::Kind::CastFromHand;
    tf.card_name = InternedName("Twinflame");
    TurnSolver::Plan base = IPlan({ tf });
    base.land_decided = true;
    CHECK(LkTurnKeeps(s, base) == std::vector<int>{ 1, 2 });
    GameState t = IBase();   // no legend of ours: nothing to copy into a group
    CHECK(LkTurnKeeps(t, base) == std::vector<int>{ 0 });
}

// ---- THE LEGEND RULE IS A STATE-BASED ACTION (CR 704.3 / 704.5j): checked at the land play, after every resolution and
// in the end step -- not first at the next beginning of combat (LegendRuleSbaCheckpoint). ----
TEST_CASE("LK SBA: a MAIN-2 second Grey Havens is legend-ruled at the drop -- only one copy untaps next turn")
{
    for (int keep : { 1, 2 })
    {
        CAPTURE(keep);
        TurnSolver::Plan plan = LkLandPlan(kHavens);
        plan.legend_keep_turn = static_cast<std::int8_t>(keep);
        GameState roll = LkHavensBoard();
        roll.phase = Phase::PostCombatMain;
        TurnSolver::ApplyPlan(roll, plan, /*is_pre_combat=*/false);
        GameState exec = LkHavensBoard();
        exec.phase = Phase::PostCombatMain;
        LkExecDrop(exec, plan, 20);
        const int survivor = keep == 2 ? 20 : 10, loser = keep == 2 ? 10 : 20;
        for (GameState* g : { &roll, &exec })
        {
            CHECK(IFind(*g, survivor) != nullptr);
            CHECK(IFind(*g, loser) == nullptr);
            CHECK(InGraveyard(*g, loser));
        }
        // ...so the next turn untaps ONE Havens: two copies can never both tap for mana in its main 1.
        REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(roll));
        CHECK(std::count_if(roll.battlefield.begin(), roll.battlefield.end(),
                            [](const Permanent& p) { return p.card.m_name == kHavens; }) == 1);
    }
}

namespace {
// Surrak #10 on the battlefield, a second Surrak #20 in the library, Green Sun's Zenith #30 in hand, seven Forests.
GameState LkGszBoard()
{
    GameState s = IBase();
    s.phase = Phase::PostCombatMain;
    IPut(s, kSurrak, 10);
    s.players[0].library.push_back(ICard(kSurrak, 20));
    for (int k = 0; k < 7; ++k) { IPut(s, "Forest", 40 + k); }
    return s;
}
}   // namespace

TEST_CASE("LK SBA: a non-legendary resolution's put (GSZ -> a second Surrak) is legend-ruled at the resolution, both worlds")
{
    for (int keep : { 1, 2 })
    {
        CAPTURE(keep);
        const int survivor = keep == 2 ? 20 : 10, loser = keep == 2 ? 10 : 20;
        {   // the rollout: a main-2 GSZ (no combat follows in this apply)
            GameState roll = LkGszBoard();
            roll.players[0].hand.push_back(ICard("Green Sun's Zenith", 30));
            Action gsz = ICast("Green Sun's Zenith", 0);
            gsz.chosen_x     = 6;
            gsz.tutor_target = InternedName(kSurrak);
            TurnSolver::Plan plan = IPlan({ gsz });
            plan.land_decided     = true;
            plan.legend_keep_turn = static_cast<std::int8_t>(keep);
            TurnSolver::ApplyPlan(roll, plan, /*is_pre_combat=*/false);
            CHECK(IFind(roll, survivor) != nullptr);
            CHECK(IFind(roll, loser) == nullptr);
        }
        {   // the executor: GameEngine::ResolveStack's post-resolution SBA
            GameState exec = LkGszBoard();
            exec.scripted_legend_keep = static_cast<std::int8_t>(keep);
            StackEntry e;
            e.source           = ICard("Green Sun's Zenith", 30);
            e.controller_index = 0;
            e.chosen_x         = 6;
            e.tutor_target     = kSurrak;
            exec.stack.push_back(e);
            AIEngine eng(MulliganProfile::DefaultProfile(), 0, 0);
            GameEngine ge(eng);
            MtgTestSeam::GeResolveStack(ge, exec);
            CHECK(IFind(exec, survivor) != nullptr);
            CHECK(IFind(exec, loser) == nullptr);
        }
    }
}

TEST_CASE("LK SBA: a duplicate left at the end step is legend-ruled there (executor and rollout), by the turn's pin")
{
    for (int keep : { 1, 2 })
    {
        CAPTURE(keep);
        const int survivor = keep == 2 ? 20 : 10, loser = keep == 2 ? 10 : 20;
        GameState base = IBase();
        base.phase = Phase::Ending;
        IPut(base, kSurrak, 10);
        IPut(base, kSurrak, 20, 0, /*sick=*/true);   // as an end-step return / put would leave it
        base.scripted_legend_keep = static_cast<std::int8_t>(keep);
        GameState exec = base;
        AIEngine eng(MulliganProfile::DefaultProfile(), 0, 0);
        GameEngine ge(eng);
        MtgTestSeam::GeEndStep(ge, exec);
        GameState roll = base;
        REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(roll));
        for (const GameState* g : { &exec, &roll })
        {
            CHECK(IFind(*g, survivor) != nullptr);
            CHECK(IFind(*g, loser) == nullptr);
        }
    }
}

TEST_CASE("LK put: Galadriel of Lothlorien's scry-reveal land put of a second Grey Havens is twinned (the T5 Galadriel miss)")
{
    LkSearchedScope searched;
    GameState s = IBase();
    IPut(s, "Galadriel of Lothl\xC3\xB3rien", 5);
    IPut(s, kHavens, 10);
    s.players[0].library.push_back(ICard(kHavens, 20));
    TurnSolver::Plan base;
    base.land_decided = true;
    CHECK(LkTurnKeeps(s, base) == std::vector<int>{ 1, 2 });
    {   // no Galadriel: nothing can put the library Havens
        GameState t = s;
        t.battlefield.erase(t.battlefield.begin());
        CHECK(LkTurnKeeps(t, base) == std::vector<int>{ 0 });
    }
}

TEST_CASE("LK land: the EXECUTOR's main-1 land drop reads the turn pin (written before the drop, as the rollout does)")
{
    for (int keep : { 1, 2 })
    {
        CAPTURE(keep);
        TurnSolver::Plan plan = LkLandPlan(kHavens);
        plan.legend_keep_turn = static_cast<std::int8_t>(keep);
        GameState exec = LkHavensBoard();
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::CommitPhasePlan(eng, plan, true);
        eng.TakeTurn(exec, true, IResolveStack);
        const int survivor = keep == 2 ? 20 : 10, loser = keep == 2 ? 10 : 20;
        CHECK(IFind(exec, survivor) != nullptr);
        CHECK(IFind(exec, loser) == nullptr);
    }
}

// ---- Boars LK3: the upkeep-only legend routes take their keep from the searched next-upkeep pin ------------------
namespace
{
// Turn 5's end: an OLD Marit Lage token (made by an earlier upkeep; untapped, not sick) beside a second Marit Lage's
// Slumber and ten snow lands, so the next upkeep's Slumber makes a NEW (summoning-sick) Marit Lage: copies that DIFFER.
GameState SlumberBase(int* old_token)
{
    GameState s = IBase();
    for (int k = 0; k < 10; ++k) { IPut(s, "Snow-Covered Forest", 100 + k); }
    IPut(s, "Marit Lage's Slumber", 200);
    PerformUpkeepSlumber(s);   // the first Slumber's token (11 snow -> it triggers), aged below
    *old_token = 0;
    for (Permanent& p : s.battlefield)
    { if (p.is_token && p.card.HasSupertype(Supertype::Legendary)) { p.entered_this_turn = false; *old_token = p.card.m_number; } }
    REQUIRE(*old_token != 0);
    IPut(s, "Marit Lage's Slumber", 201);   // 10 lands + this Slumber = 11 snow permanents
    s.phase = Phase::Ending;
    return s;
}
int MaritLages(const GameState& s, int* survivor)
{
    int n = 0;
    for (const Permanent& p : s.battlefield)
    { if (p.is_token && p.card.HasSupertype(Supertype::Legendary)) { ++n; *survivor = p.card.m_number; } }
    return n;
}
}   // namespace

TEST_CASE("LK3 upkeep: Marit Lage's Slumber's second token reads the NEXT-UPKEEP pin -- both keeps, both worlds agree")
{
    for (int pin : { 1, 2 })
    {
        CAPTURE(pin);
        int old_token = 0;
        GameState base = SlumberBase(&old_token);
        // The pin rides the plan of the turn that crosses (WritePostApplyStatePins), beside a turn keep it must not touch.
        TurnSolver::Plan plan;
        plan.legend_keep_turn = 2;
        plan.upkeep_legend_keep_choice = static_cast<std::int8_t>(pin);
        TurnSolver::WritePostApplyStatePins(base, plan);
        CHECK(LegendKeepNow(base) == 2);
        CHECK(LegendKeepUpkeepPin(base) == pin);
        // Rollout: the whole crossing.
        GameState roll = base;
        REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(roll));
        // Executor: the untap opens the upkeep pin, the upkeep's Slumber makes the token, the upkeep's end closes it.
        GameState exec = base;
        exec.phase = Phase::Beginning;
        AIEngine eng(MulliganProfile::DefaultProfile(), 0, 0);
        GameEngine ge(eng);
        MtgTestSeam::GeUntapStep(ge, exec);
        CHECK(LegendKeepNow(exec) == pin);   // the upkeep pin is the keep in force through the upkeep
        MtgTestSeam::GeUpkeepStep(ge, exec);
        for (const GameState* g : { &roll, &exec })
        {
            int survivor = 0;
            REQUIRE(MaritLages(*g, &survivor) == 1);
            if (pin == 1) { CHECK(survivor == old_token); }    // keep the oldest: the untapped, non-sick 20/20
            else          { CHECK(survivor != old_token); }    // keep the newest: the fresh token
            CHECK(g->scripted_legend_keep == 0);               // consumed: no pin leaks into the turn's main phase
        }
    }
}

TEST_CASE("LK3 upkeep: the BoundaryFan fans the upkeep legend keep (provider's pick first, then the other); none without a differing pair")
{
    int old_token = 0;
    GameState base = SlumberBase(&old_token);
    const auto crossings = TurnSolver::UpkeepLegendBoundaryForTest(base);
    REQUIRE(crossings.size() == 2);
    // Rank 0 follows the provider (keep the oldest by default: the non-sick token survives) and records 3; rank 1
    // records the other answer (keep the newest: the sick token survives).
    CHECK(crossings[0].first == kLegendKeepUpkeepFollow);
    CHECK(crossings[0].second == 0);
    CHECK(crossings[1].first == 2);
    CHECK(crossings[1].second == 1);
    // A state whose pin already covers the upkeep is not fanned again.
    GameState covered = base;
    SetLegendKeepUpkeepPin(covered, 2);
    for (const auto& c : TurnSolver::UpkeepLegendBoundaryForTest(covered)) { CHECK(c.first == -2); }
    // No legend of ours on the battlefield: the Slumber's lone token has nothing to duplicate -> width 1, no pin.
    GameState lone = base;
    lone.battlefield.erase(std::remove_if(lone.battlefield.begin(), lone.battlefield.end(),
                                          [](const Permanent& p) { return p.is_token; }), lone.battlefield.end());
    for (const auto& c : TurnSolver::UpkeepLegendBoundaryForTest(lone)) { CHECK(c.first == -2); }
}
