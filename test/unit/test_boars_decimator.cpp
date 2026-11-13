// Boars I4 (2026-11-13): Decimator of the Provinces -- "Emerge {6}{G}{G}{G}" (CR 702.119: an ALTERNATIVE cost,
// sacrifice a creature and pay the emerge cost reduced by its mana value -- generic only) and "When you cast this
// spell, creatures you control get +2/+2 and gain trample until end of turn" (cast_trigger_team_pump_*).
// Checked in BOTH worlds: the rollout's apply_one (TurnSolver::ApplyPlan) and the executor's CastSpellFromHand.
#include <doctest/doctest.h>
#include "ai/AIEngine.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"
#include <algorithm>
#include <string>
#include <vector>

namespace
{
const char* const kDec = "Decimator of the Provinces";
const CardDefinition& DDef(const char* name)
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
Card DCard(const char* name, int number)
{
    Card c = DDef(name).card;
    c.m_number = number;
    return c;
}
Permanent& DPut(GameState& s, const char* name, int number, bool sick = false)
{
    Permanent p;
    p.card = DCard(name, number);
    p.controller_index = p.owner_index = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}
GameState DBase()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(DCard("Forest", 900 + k)); }
    return s;
}
void DForests(GameState& s, int n) { for (int k = 0; k < n; ++k) { DPut(s, "Forest", 50 + k); } }
void DHorn(GameState& s, int number)
{
    Permanent& h = DPut(s, "Herald's Horn", number);
    h.chosen_subtype_id = SubtypeRegistry::Instance().Id("Boar");
}
const Permanent* DNum(const GameState& s, int num)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == num) { return &p; } }
    return nullptr;
}
bool InGy(const GameState& s, int num)
{
    for (const Card& c : s.players[0].graveyard) { if (c.m_number == num) { return true; } }
    return false;
}
Action DEmerge(int victim, const std::string& hand_victim = std::string())
{
    Action a;
    a.kind                 = Action::Kind::CastFromHand;
    a.card_name            = kDec;
    a.def                  = &DDef(kDec);
    a.hand_index           = -1;
    a.emerge               = true;
    a.soulfire_own_targets = victim;
    a.trick_hand_target    = hand_victim;
    a.card_mv              = 10;
    return a;
}
Action DCast(const char* name)
{
    Action a;
    a.kind           = Action::Kind::CastFromHand;
    a.card_name      = name;
    a.def            = &DDef(name);
    a.hand_index     = -1;
    a.cost           = DDef(name).card.m_mana_cost;
    a.is_noncreature = !DDef(name).card.IsCreature();
    return a;
}
TurnSolver::Plan DPlan(const std::vector<Action>& acts) { TurnSolver::Plan p; p.actions = acts; return p; }
// The executor: cast Decimator (the last hand card) in emerge mode off `victim`.
void ExecEmerge(GameState& s, int victim)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    ManaPool avail = AvailableManaPool(s);
    MtgTestSeam::CastEmergeFromHand(eng, s, s.players[0].hand.back(), avail, victim);
}
}   // namespace

TEST_CASE("Decimator of the Provinces: card data")
{
    const CardDefinition& d = DDef(kDec);
    CHECK(d.card.m_mana_cost.ManaValue() == 10);
    REQUIRE(d.params.emerge_cost.has_value());
    CHECK(d.params.emerge_cost->ManaValue() == 9);
    CHECK(d.params.emerge_cost->green == 3);
    CHECK(d.params.cast_trigger_team_pump_power == 2);
    CHECK(d.params.cast_trigger_team_pump_tough == 2);
    CHECK(d.card.HasKeyword(Keyword::Haste));
    CHECK(CastSacsCreature(d.params, /*emerge=*/true));
    CHECK_FALSE(CastSacsCreature(d.params, /*emerge=*/false));   // the hard cast sacrifices nothing
    CHECK_FALSE(HasSacCreatureAdditionalCost(d.params));
}

TEST_CASE("Decimator: the emerge bill per victim MV -- generic only, floored at 0; Herald's Horn stacks")
{
    const CardDefinition& d = DDef(kDec);
    struct Row { int mv; int horns; int generic; };
    for (const Row r : { Row{0, 0, 6}, Row{1, 0, 5}, Row{2, 0, 4}, Row{4, 0, 2}, Row{8, 0, 0}, Row{0, 1, 5},
                         Row{4, 1, 1}, Row{4, 2, 0}, Row{8, 1, 0}, Row{5, 2, 0} })
    {
        CAPTURE(r.mv); CAPTURE(r.horns);
        GameState s = DBase();
        for (int h = 0; h < r.horns; ++h) { DHorn(s, 20 + h); }
        const ManaCost c = EmergeCastCost(d, s, r.mv);
        CHECK(c.generic == r.generic);
        CHECK(c.green == 3);   // the coloured pips are never reduced
        CHECK(c.ManaValue() == r.generic + 3);
    }
    SUBCASE("victim MV off the live board and off the hand; End-Raze Forerunners pays exactly {G}{G}{G}")
    {
        GameState s = DBase();
        DPut(s, "End-Raze Forerunners", 30);
        DPut(s, "Birds of Paradise", 31);
        s.players[0].hand.push_back(DCard("Herald of Ilharg", 32));
        CHECK(EmergeVictimMv(s, 30) == 8);
        CHECK(EmergeVictimMv(s, 31) == 1);
        CHECK(EmergeVictimMv(s, 32) == 4);
        CHECK(EmergeVictimMv(s, 999) == 0);
        const ManaCost c = EmergeCastCost(d, s, EmergeVictimMv(s, 30));
        CHECK(c.ManaValue() == 3);
        CHECK(c.green == 3);
    }
}

TEST_CASE("Decimator: enumeration -- one emerge variant per victim class beside the hard cast; none with no creature")
{
    auto emerges = [](const GameState& s) {
        std::vector<std::pair<int, int>> out;   // (victim, bill MV)
        bool hard = false;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
        {
            for (const Action& a : p.actions)
            {
                if (a.kind != Action::Kind::CastFromHand || a.card_name.str() != kDec) { continue; }
                if (!a.emerge) { hard = true; continue; }
                const std::pair<int, int> v{ a.soulfire_own_targets, a.cost.ManaValue() };
                if (std::find(out.begin(), out.end(), v) == out.end()) { out.push_back(v); }
            }
        }
        return std::make_pair(out, hard);
    };
    SUBCASE("End-Raze and Birds on the battlefield, 8 Forests: both victims are offered, priced 3 and 8")
    {
        GameState s = DBase();
        DForests(s, 7);
        DPut(s, "End-Raze Forerunners", 30);
        DPut(s, "Birds of Paradise", 31);
        s.players[0].hand.push_back(DCard(kDec, 60));
        const auto r = emerges(s);
        CHECK(std::find(r.first.begin(), r.first.end(), std::make_pair(30, 3)) != r.first.end());
        CHECK(std::find(r.first.begin(), r.first.end(), std::make_pair(31, 8)) != r.first.end());
    }
    SUBCASE("no creature anywhere: no emerge variant (and 10 lands still find the hard cast)")
    {
        GameState s = DBase();
        DForests(s, 10);
        s.players[0].hand.push_back(DCard(kDec, 60));
        const auto r = emerges(s);
        CHECK(r.first.empty());
        CHECK(r.second);
    }
}

TEST_CASE("Decimator: the victim taps for this very cast, then dies -- both worlds")
{
    // Birds of Paradise (MV 1) -> {5}{G}{G}{G} = 8, on 7 Forests + Birds: payable ONLY if Birds taps first.
    auto board = [] {
        GameState s = DBase();
        DForests(s, 7);
        DPut(s, "Birds of Paradise", 31);
        s.players[0].hand.push_back(DCard(kDec, 60));
        return s;
    };
    g_cast_trigger_mana_spent_probe = -1;
    GameState roll = board();
    TurnSolver::ApplyPlan(roll, DPlan({ DEmerge(31) }), /*is_pre_combat=*/true);
    CHECK(DNum(roll, 60) != nullptr);
    CHECK(DNum(roll, 31) == nullptr);
    CHECK(InGy(roll, 31));
    CHECK(g_cast_trigger_mana_spent_probe == 8);

    g_cast_trigger_mana_spent_probe = -1;
    GameState exec = board();
    ExecEmerge(exec, 31);
    CHECK(DNum(exec, 31) == nullptr);
    CHECK(InGy(exec, 31));
    CHECK(g_cast_trigger_mana_spent_probe == 8);
    CHECK(exec.players[0].hand.empty());
}

TEST_CASE("Decimator: mana_spent is the emerge bill actually paid (Horn included) in both worlds")
{
    for (const bool horn : { false, true })
    {
        CAPTURE(horn);
        auto board = [horn] {
            GameState s = DBase();
            DForests(s, 9);
            if (horn) { DHorn(s, 20); }
            DPut(s, "End-Raze Forerunners", 30);
            DPut(s, "Birds of Paradise", 31, /*sick=*/true);
            s.players[0].hand.push_back(DCard(kDec, 60));
            return s;
        };
        // Birds (MV 1): 8, 7 with a Horn. End-Raze (MV 8): 3 either way (the Horn's {1} has nothing left to take).
        for (const int victim : { 31, 30 })
        {
            CAPTURE(victim);
            const int want = victim == 30 ? 3 : (horn ? 7 : 8);
            g_cast_trigger_mana_spent_probe = -1;
            GameState roll = board();
            TurnSolver::ApplyPlan(roll, DPlan({ DEmerge(victim) }), true);
            CHECK(g_cast_trigger_mana_spent_probe == want);
            CHECK(DNum(roll, victim) == nullptr);
            g_cast_trigger_mana_spent_probe = -1;
            GameState exec = board();
            ExecEmerge(exec, victim);
            CHECK(g_cast_trigger_mana_spent_probe == want);
            CHECK(DNum(exec, victim) == nullptr);
        }
    }
}

TEST_CASE("Decimator: Herald of Ilharg sees the MV-10 cast -- unless it IS the victim")
{
    auto board = [] {
        GameState s = DBase();
        DForests(s, 9);
        DPut(s, "Herald of Ilharg", 10);
        DPut(s, "End-Raze Forerunners", 30);
        s.players[0].hand.push_back(DCard(kDec, 60));
        return s;
    };
    SUBCASE("End-Raze is the victim: Herald gets 2 counters and pings for 2 (MV stays 10)")
    {
        GameState roll = board();
        TurnSolver::ApplyPlan(roll, DPlan({ DEmerge(30) }), true);
        REQUIRE(DNum(roll, 10) != nullptr);
        CHECK(TotalCountersOn(*DNum(roll, 10)) == 2);
        CHECK(roll.players[1].life == 18);
        GameState exec = board();
        ExecEmerge(exec, 30);
        REQUIRE(DNum(exec, 10) != nullptr);
        CHECK(TotalCountersOn(*DNum(exec, 10)) == 2);
        CHECK(exec.players[1].life == 18);
    }
    SUBCASE("Herald is the victim: gone before the cast triggers -- no counters, no damage")
    {
        GameState roll = board();
        TurnSolver::ApplyPlan(roll, DPlan({ DEmerge(10) }), true);
        CHECK(DNum(roll, 10) == nullptr);
        CHECK(InGy(roll, 10));
        CHECK(roll.players[1].life == 20);
        GameState exec = board();
        ExecEmerge(exec, 10);
        CHECK(DNum(exec, 10) == nullptr);
        CHECK(exec.players[1].life == 20);
    }
}

TEST_CASE("Decimator: the cast-trigger team pump hits the others, never Decimator itself -- both worlds")
{
    auto board = [] {
        GameState s = DBase();
        DForests(s, 9);
        DPut(s, "Birds of Paradise", 31);
        DPut(s, "End-Raze Forerunners", 30);
        s.players[0].hand.push_back(DCard(kDec, 60));
        return s;
    };
    GameState roll = board();
    TurnSolver::ApplyPlan(roll, DPlan({ DEmerge(30) }), true);
    REQUIRE(DNum(roll, 31) != nullptr);
    REQUIRE(DNum(roll, 60) != nullptr);
    CHECK(DNum(roll, 31)->temp_power_bonus == 2);
    CHECK(DNum(roll, 31)->temp_tough_bonus == 2);
    CHECK(DNum(roll, 60)->temp_power_bonus == 0);

    GameState exec = board();
    ExecEmerge(exec, 30);
    // The trigger sits ABOVE the spell; resolve it, then the spell.
    REQUIRE(exec.stack.size() == 2);
    CHECK(exec.stack.back().trigger_kind == StackEntry::TriggerKind::CastTeamPump);
    for (int k = 0; k < 2; ++k)
    {
        const StackEntry e = exec.stack.back();
        exec.stack.pop_back();
        EffectHandler::Resolve(exec, e, DDef(kDec));
    }
    REQUIRE(DNum(exec, 31) != nullptr);
    REQUIRE(DNum(exec, 60) != nullptr);
    CHECK(DNum(exec, 31)->temp_power_bonus == 2);
    CHECK(DNum(exec, 60)->temp_power_bonus == 0);
}

TEST_CASE("Decimator: the HARD cast sacrifices nothing (and still fires the pump)")
{
    GameState s = DBase();
    DForests(s, 10);
    DPut(s, "Birds of Paradise", 31, /*sick=*/true);
    s.players[0].hand.push_back(DCard(kDec, 60));
    g_cast_trigger_mana_spent_probe = -1;
    TurnSolver::ApplyPlan(s, DPlan({ DCast(kDec) }), true);
    CHECK(DNum(s, 60) != nullptr);
    REQUIRE(DNum(s, 31) != nullptr);
    CHECK(DNum(s, 31)->temp_power_bonus == 2);
    CHECK(g_cast_trigger_mana_spent_probe == 10);
}

TEST_CASE("Decimator: a same-plan HAND victim is cast BEFORE the emerge whatever the listed order")
{
    // Flinthoof Boar (MV 2) cast this turn, then eaten by the emerge ({4}{G}{G}{G}): 9 of 10 Forests.
    GameState s = DBase();
    DForests(s, 10);
    s.players[0].hand.push_back(DCard("Flinthoof Boar", 61));
    s.players[0].hand.push_back(DCard(kDec, 60));
    TurnSolver::ApplyPlan(s, DPlan({ DEmerge(61, "Flinthoof Boar"), DCast("Flinthoof Boar") }), true);
    CHECK(DNum(s, 60) != nullptr);   // Decimator resolved
    CHECK(DNum(s, 61) == nullptr);   // the Boar entered and was sacrificed
    CHECK(InGy(s, 61));
}
