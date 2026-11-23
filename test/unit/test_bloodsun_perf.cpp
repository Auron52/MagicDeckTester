// Blood Sun perf step (docs/design/blood-sun-pipeline.md §14 "Performance diagnosis"): the go-off front's test
// (BloodSunGoOffRank, ORDER only) and the Grim Tutor B12 arms (DEFAULT OFF until measured, USER 2026-11-21: "We should
// probably be measuring branching and performance more first.").
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/RevelationPackages.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace
{
void EnsureCardsPf()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}
const CardDefinition& DefPf(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}
Card CardPf(const std::string& name, int number)
{
    Card c = DefPf(name).card;
    c.m_number = number;
    return c;
}
void PutPf(GameState& s, const std::string& name, int number, int controller = 0)
{
    Permanent p;
    p.card = CardPf(name, number);
    p.controller_index = controller;
    p.owner_index = controller;
    s.battlefield.push_back(p);
}
const BloodSunProvider& ProvPf()
{
    static const BloodSunProvider bp;
    return bp;
}
// Blood Sun out, `swamps` untapped Swamps, the opponent at `opp`.
GameState BasePf(int swamps, int opp)
{
    EnsureCardsPf();
    GameState s;
    s.m_provider          = &ProvPf();
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = opp;
    s.deck_has_land_ability_loss = true;
    s.own_death_live      = true;
    PutPf(s, "Blood Sun", 1);
    for (int i = 0; i < swamps; ++i) { PutPf(s, "Swamp", 100 + i); }
    return s;
}
Action CastPf(const std::string& name, const std::string& pkg = std::string(), int x = 0)
{
    const CardDefinition& d = DefPf(name);
    Action a;
    a.kind        = Action::Kind::CastFromHand;
    a.card_name   = InternedName(name);
    a.def         = &d;
    a.cost        = d.card.m_mana_cost;
    a.cost.generic += x;
    a.chosen_x    = x;
    if (!pkg.empty()) { a.tutor_target = InternedName(pkg); }
    return a;
}
struct GrimArms
{
    GrimArms(int prune, int gate)
    {
        heurarm::t_arm[heurarm::BLOODSUN_USER_PRUNE]    = 1;
        heurarm::t_arm[heurarm::BLOODSUN_GRIM_PRUNE]    = static_cast<std::int8_t>(prune);
        heurarm::t_arm[heurarm::BLOODSUN_GRIM_REV_GATE] = static_cast<std::int8_t>(gate);
    }
    ~GrimArms()
    {
        heurarm::t_arm[heurarm::BLOODSUN_USER_PRUNE]    = -1;
        heurarm::t_arm[heurarm::BLOODSUN_GRIM_PRUNE]    = -1;
        heurarm::t_arm[heurarm::BLOODSUN_GRIM_REV_GATE] = -1;
    }
};
bool HasPf(const std::vector<std::string>& v, const std::string& x)
{ return std::find(v.begin(), v.end(), x) != v.end(); }
struct BucketArmPf
{
    heurarm::Arm saved;
    BucketArmPf() : saved(heurarm::t_arm) { heurarm::t_arm[heurarm::BLOODSUN_BUCKET_DISCARD] = 1; }
    ~BucketArmPf() { heurarm::t_arm = saved; }
};
struct PriorityArmPf
{
    heurarm::Arm saved;
    explicit PriorityArmPf(bool on) : saved(heurarm::t_arm)
    { heurarm::t_arm[heurarm::BLOODSUN_REV_PRIORITY] = on ? 1 : 0; }
    ~PriorityArmPf() { heurarm::t_arm = saved; }
};
std::string FirstShedPf(const GameState& s)
{
    const std::vector<int> rank = ProvPf().CleanupDiscardCandidates(s, nullptr);
    REQUIRE_FALSE(rank.empty());
    return s.players[0].hand[static_cast<std::size_t>(rank.front())].m_name.str();
}
}   // namespace

TEST_CASE("Blood Sun B14: keep three lands ahead of the second drop-maker")
{
    GameState s = BasePf(0, 20);
    s.battlefield.clear();
    const std::vector<std::string> names = { "Blood Sun", "Diabolic Revelation", "Time Warp",
        "Exploration", "Burgeoning", "City of Brass", "Blooming Marsh", "Lotus Field" };
    int num = 300;
    for (const std::string& name : names) { s.players[0].hand.push_back(CardPf(name, num++)); }
    const BucketArmPf arm;
    CHECK(heurarm::Flag(heurarm::BLOODSUN_BUCKET_DISCARD, false));
    CHECK_FALSE(ProvPf().CleanupDiscardShedStable());
    CHECK(FirstShedPf(s) == "Burgeoning");
}

TEST_CASE("Blood Sun B14: Fastbond on the board makes other drop-makers surplus")
{
    GameState s = BasePf(0, 20);
    s.battlefield.clear();
    PutPf(s, "Fastbond", 200);
    const std::vector<std::string> names = { "Blood Sun", "Diabolic Revelation", "Time Warp",
        "Exploration", "Burgeoning", "City of Brass", "Blooming Marsh", "Lotus Field" };
    int num = 300;
    for (const std::string& name : names) { s.players[0].hand.push_back(CardPf(name, num++)); }
    const BucketArmPf arm;
    const std::string first = FirstShedPf(s);
    CHECK((first == "Exploration" || first == "Burgeoning"));
}

TEST_CASE("Blood Sun B14: a Lotus supplies both black pips from the battlefield")
{
    GameState s = BasePf(0, 20);
    PutPf(s, "Lotus Field", 201);
    const std::vector<std::string> names =
        { "Blooming Marsh", "City of Brass", "Lotus Vale", "Scorched Ruins" };
    int num = 300;
    for (const std::string& name : names) { s.players[0].hand.push_back(CardPf(name, num++)); }
    const BucketArmPf arm;
    const std::string first = FirstShedPf(s);
    CHECK((first == "Blooming Marsh" || first == "City of Brass"));
}

TEST_CASE("Blood Sun go-off front: Emrakul + Time Warp at 20 life is a projected kill this round")
{
    GameState s = BasePf(25, 20);
    const std::vector<Action> cands = { CastPf("Emrakul, the Aeons Torn"), CastPf("Time Warp") };
    // Emrakul alone: one extra turn, 15 power -> 15 < 20, not a kill.
    CHECK(BloodSunGoOffRank(s, cands, { 0 }) == 0);
    // Emrakul + Time Warp: two extra turns of 15 -> 30 >= 20.
    CHECK(BloodSunGoOffRank(s, cands, { 0, 1 }) == 2);
    // ...and at 15 life Emrakul alone is enough.
    s.players[1].life = 15;
    CHECK(BloodSunGoOffRank(s, cands, { 0 }) == 2);
    // Time Warp alone with no creature: no damage.
    CHECK(BloodSunGoOffRank(s, cands, { 1 }) == 0);
}

TEST_CASE("Blood Sun go-off front: a Revelation fetching Emrakul ranks first only with 15 mana left to cast it")
{
    const std::string pkg = revpkg::EncodePackage(0, { "Emrakul, the Aeons Torn" });
    {
        GameState s = BasePf(21, 20);   // 21 - 6 (X = 1) = 15 left
        const std::vector<Action> cands = { CastPf("Diabolic Revelation", pkg, 1) };
        CHECK(BloodSunGoOffRank(s, cands, { 0 }) == 1);
    }
    {
        GameState s = BasePf(20, 20);   // 14 left: not yet
        const std::vector<Action> cands = { CastPf("Diabolic Revelation", pkg, 1) };
        CHECK(BloodSunGoOffRank(s, cands, { 0 }) == 0);
    }
}

TEST_CASE("Blood Sun go-off front: Revelation's two-Lotus Exploration bridge needs an open drop")
{
    GameState s = BasePf(10, 20);
    const std::string pkg = revpkg::EncodePackage(0,
        { "Time Warp", "Diabolic Revelation", "Exploration", "Lotus Vale", "Lotus Field" });
    const std::vector<Action> cands = { CastPf("Diabolic Revelation", pkg, 5) };
    CHECK(BloodSunGoOffRank(s, cands, { 0 }) == 1);
    s.players[0].lands_played_this_turn = s.players[0].LandDropsAvailable();
    CHECK(BloodSunGoOffRank(s, cands, { 0 }) == 0);
    s.players[0].lands_played_this_turn = 0;
    s.players[0].hand.push_back(CardPf("Time Warp", 900));
    const std::string hand_warp_pkg = revpkg::EncodePackage(0,
        { "Diabolic Revelation", "Exploration", "Lotus Vale", "Lotus Field" });
    CHECK(BloodSunGoOffRank(s, { CastPf("Diabolic Revelation", hand_warp_pkg, 4) }, { 0 }) == 1);
}

TEST_CASE("Blood Sun go-off front: Fastbond buys two drops only with held green and life")
{
    GameState s = BasePf(10, 20);
    PutPf(s, "City of Brass", 200);
    s.players[0].lands_played_this_turn = s.players[0].LandDropsAvailable();
    const std::string pkg = revpkg::EncodePackage(0,
        { "Time Warp", "Diabolic Revelation", "Fastbond", "Lotus Vale", "Lotus Field" });
    const std::vector<Action> cands = { CastPf("Diabolic Revelation", pkg, 5) };
    CHECK(BloodSunGoOffRank(s, cands, { 0 }) == 1);
    const std::string larger_pkg = revpkg::EncodePackage(0,
        { "Time Warp", "Diabolic Revelation", "Fastbond", "Lotus Vale", "Lotus Field", "Ulamog, the Infinite Gyre" });
    const std::vector<Action> two_packages = { cands[0], CastPf("Diabolic Revelation", larger_pkg, 6) };
    CHECK(BloodSunGoOffRank(s, two_packages, { 0, 1 }) == 1);
    s.players[0].life = 2;
    CHECK(BloodSunGoOffRank(s, cands, { 0 }) == 0);
    GameState no_green = BasePf(11, 20);
    no_green.players[0].lands_played_this_turn = no_green.players[0].LandDropsAvailable();
    CHECK(BloodSunGoOffRank(no_green, cands, { 0 }) == 0);
}

TEST_CASE("Blood Sun go-off front: ORDER only, off under MTG_BLOODSUN_GOOFF's human-play guard")
{
    GameState s = BasePf(25, 15);
    const std::vector<Action> cands = { CastPf("Emrakul, the Aeons Torn") };
    CHECK(ProvPf().PlanMoveOrderFront(s, cands, { 0 }, false) == BloodSunGoOffOn());
}

TEST_CASE("Blood Sun Revelation priority reaches the saved bridges at high life without duplicate wave-0 actions")
{
    GameState s = BasePf(10, 20);
    s.players[0].hand.push_back(CardPf("Diabolic Revelation", 300));
    s.players[0].hand.push_back(CardPf("Scorched Ruins", 301));
    const std::vector<std::string> lib = { "Scorched Ruins", "Scorched Ruins", "Scorched Ruins",
        "Scorched Ruins", "Diabolic Revelation", "Exploration", "Lotus Vale", "Lotus Vale",
        "Time Warp", "Fastbond" };
    int num = 400;
    for (const std::string& nm : lib) { s.players[0].library.push_back(CardPf(nm, num++)); }
    const std::vector<std::string> bridge = { "Diabolic Revelation", "Exploration",
        "Lotus Vale", "Lotus Vale", "Time Warp" };
    auto wave0 = [&](const GameState& x)
    {
        std::set<std::string> enc;
        std::set<std::vector<std::string>> packs;
        for (const Action& a : TurnSolver::CollectActionsForTest(x, true))
        {
            if (a.card_name.str() != "Diabolic Revelation" || !revpkg::IsPackage(a.tutor_target.str()))
            { continue; }
            CHECK(revpkg::PackageWave(a.tutor_target.str()) == 0);
            CHECK(enc.insert(a.tutor_target.str()).second);
            packs.insert(revpkg::DecodePackage(a.tutor_target.str()));
        }
        return packs;
    };
    {
        const PriorityArmPf off(false);
        CHECK(wave0(s).count(bridge) == 0);
    }
    {
        const PriorityArmPf on(true);
        CHECK(wave0(s).count(bridge) == 1);
        GameState held = s;
        held.players[0].hand.push_back(CardPf("Emrakul, the Aeons Torn", 302));
        held.players[0].hand.push_back(CardPf("Blooming Marsh", 303));
        const std::vector<std::string> hold = { "Fastbond", "Lotus Vale", "Time Warp" };
        CHECK(wave0(held).count(hold) == 1);
        GameState grim = s;
        grim.players[0].hand.push_back(CardPf("Grim Tutor", 304));
        CHECK(wave0(grim).count(bridge) == 0);
    }
}

TEST_CASE("Blood Sun B12 arm: Grim Tutor's own targets (lever on) vs the B3 list (lever off)")
{
    GameState s = BasePf(4, 20);
    s.players[0].hand.push_back(CardPf("Grim Tutor", 40));
    for (const char* n : { "Diabolic Revelation", "Time Warp", "Emrakul, the Aeons Torn", "Fastbond", "Scorched Ruins" })
    { s.players[0].library.push_back(CardPf(n, 500 + static_cast<int>(s.players[0].library.size()))); }
    const CardParams& gp = DefPf("Grim Tutor").params;
    {
        GrimArms arms(0, 0);   // B3 list
        const std::vector<std::string> t = ProvPf().TutorCandidates(s, 0, gp);
        CHECK(HasPf(t, "Time Warp"));
        CHECK(HasPf(t, "Emrakul, the Aeons Torn"));
        CHECK(HasPf(t, "Diabolic Revelation"));
    }
    {
        GrimArms arms(1, 0);   // B12: Revelation (none in hand), the big land (4 land mana < 7); no same-turn Time Warp
        const std::vector<std::string> t = ProvPf().TutorCandidates(s, 0, gp);
        CHECK(HasPf(t, "Diabolic Revelation"));
        CHECK(HasPf(t, "Scorched Ruins"));
        CHECK_FALSE(HasPf(t, "Emrakul, the Aeons Torn"));
        CHECK_FALSE(HasPf(t, "Fastbond"));
        CHECK_FALSE(HasPf(t, "Time Warp"));   // Grim (3) + Time Warp (5) is not castable from 4 Swamps (no {U}{U})
    }
    {
        GrimArms arms(1, 0);
        GameState r = s;
        r.players[0].hand.push_back(CardPf("Diabolic Revelation", 41));
        for (int i = 0; i < 3; ++i) { PutPf(r, "Swamp", 200 + i); }   // 7 land mana: no big land either
        const std::vector<std::string> t = ProvPf().TutorCandidates(r, 0, gp);
        CHECK(t.empty());   // nothing missing -> Grim Tutor is not cast (TutorNeedsCandidate)
    }
}

TEST_CASE("Blood Sun B12 second arm: no Grim Tutor at 8+ mana with a Revelation in hand, but Grim -> Time Warp stays")
{
    GameState s = BasePf(0, 20);
    for (int i = 0; i < 9; ++i) { PutPf(s, "City of Brass", 300 + i); }   // 9 mana, every colour
    s.players[0].hand.push_back(CardPf("Grim Tutor", 40));
    s.players[0].hand.push_back(CardPf("Diabolic Revelation", 41));
    for (const char* n : { "Time Warp", "Emrakul, the Aeons Torn", "Blood Sun" })
    { s.players[0].library.push_back(CardPf(n, 500 + static_cast<int>(s.players[0].library.size()))); }
    const CardParams& gp = DefPf("Grim Tutor").params;
    {
        GrimArms arms(1, 1);
        const std::vector<std::string> t = ProvPf().TutorCandidates(s, 0, gp);
        REQUIRE(t.size() == 1);
        CHECK(t[0] == "Time Warp");   // Grim (1BB) + Time Warp (3UU) castable from 9 Cities
    }
    {
        GrimArms arms(1, 0);   // gate off: the B12 list plus the same-turn Time Warp
        const std::vector<std::string> t = ProvPf().TutorCandidates(s, 0, gp);
        CHECK(HasPf(t, "Time Warp"));
    }
}

TEST_CASE("Blood Sun Grim -> Time Warp counts a land drop still available from hand")
{
    GameState s = BasePf(0, 20);
    for (int i = 0; i < 7; ++i) { PutPf(s, "City of Brass", 300 + i); }
    s.players[0].hand.push_back(CardPf("Grim Tutor", 40));
    s.players[0].hand.push_back(CardPf("City of Brass", 41));
    s.players[0].library.push_back(CardPf("Time Warp", 500));
    const CardDefinition& tw = DefPf("Time Warp");
    CHECK(BloodSunGrimTimeWarpCastable(s, 0, tw));
    {
        GrimArms arms(1, 0);
        CHECK(HasPf(ProvPf().TutorCandidates(s, 0, DefPf("Grim Tutor").params), "Time Warp"));
    }
    s.players[0].lands_played_this_turn = s.players[0].LandDropsAvailable();
    CHECK_FALSE(BloodSunGrimTimeWarpCastable(s, 0, tw));
    GameState lotus = BasePf(7, 20);
    lotus.players[0].hand.push_back(CardPf("Grim Tutor", 46));
    lotus.players[0].hand.push_back(CardPf("Lotus Vale", 47));
    CHECK(BloodSunGrimTimeWarpCastable(lotus, 0, tw));   // the land supplies both blue pips
}

TEST_CASE("Blood Sun B12-bis: one productive Lotus, otherwise one Ruins")
{
    GrimArms old(1, 0);
    heurarm::t_arm[heurarm::BLOODSUN_GRIM_V2] = 1;
    GameState s = BasePf(0, 20);
    for (int i = 0; i < 3; ++i) { PutPf(s, "City of Brass", 300 + i); }
    s.players[0].hand.push_back(CardPf("Grim Tutor", 40));
    s.players[0].hand.push_back(CardPf("Grim Tutor", 41));
    s.players[0].hand.push_back(CardPf("Diabolic Revelation", 42));
    for (const char* n : { "Lotus Vale", "Lotus Field", "Scorched Ruins", "Fastbond" })
    { s.players[0].library.push_back(CardPf(n, 500 + static_cast<int>(s.players[0].library.size()))); }
    const CardParams& gp = DefPf("Grim Tutor").params;
    const auto t = ProvPf().TutorCandidates(s, 0, gp);
    CHECK(HasPf(t, "Lotus Vale"));
    CHECK_FALSE(HasPf(t, "Lotus Field"));
    CHECK_FALSE(HasPf(t, "Scorched Ruins"));
    CHECK_FALSE(HasPf(t, "Fastbond"));
    s.players[0].hand.erase(s.players[0].hand.begin() + 1);   // no second Grim to unlock
    const auto u = ProvPf().TutorCandidates(s, 0, gp);
    CHECK(HasPf(u, "Scorched Ruins"));
    CHECK_FALSE(HasPf(u, "Lotus Vale"));
    heurarm::t_arm[heurarm::BLOODSUN_GRIM_V2] = -1;
}

TEST_CASE("Blood Sun B12-bis: Fastbond surplus threshold and land cut are independent arms")
{
    GrimArms old(1, 0);
    heurarm::t_arm[heurarm::BLOODSUN_GRIM_V2] = 1;
    heurarm::t_arm[heurarm::BLOODSUN_GRIM_V2_FASTBOND] = 1;
    GameState s = BasePf(3, 20);
    s.players[0].hand.push_back(CardPf("Grim Tutor", 40));
    s.players[0].hand.push_back(CardPf("Diabolic Revelation", 41));
    s.players[0].hand.push_back(CardPf("Lotus Vale", 42));
    s.players[0].hand.push_back(CardPf("Swamp", 43));
    s.players[0].library.push_back(CardPf("Fastbond", 500));
    s.players[0].library.push_back(CardPf("Scorched Ruins", 501));
    const CardParams& gp = DefPf("Grim Tutor").params;
    CHECK(HasPf(ProvPf().TutorCandidates(s, 0, gp), "Fastbond"));
    heurarm::t_arm[heurarm::BLOODSUN_GRIM_V2_FB_T2] = 1;
    CHECK_FALSE(HasPf(ProvPf().TutorCandidates(s, 0, gp), "Fastbond"));
    heurarm::t_arm[heurarm::BLOODSUN_GRIM_V2_FB_T2] = -1;
    heurarm::t_arm[heurarm::BLOODSUN_GRIM_V2_LAND_CUT] = 1;
    const auto cut = ProvPf().TutorCandidates(s, 0, gp);
    CHECK(HasPf(cut, "Fastbond"));
    CHECK_FALSE(HasPf(cut, "Scorched Ruins"));
    heurarm::t_arm[heurarm::BLOODSUN_GRIM_V2_LAND_CUT] = -1;
    heurarm::t_arm[heurarm::BLOODSUN_GRIM_V2_FASTBOND] = -1;
    heurarm::t_arm[heurarm::BLOODSUN_GRIM_V2] = -1;
}

TEST_CASE("Blood Sun B12-bis: red-source arms and only-missing arm")
{
    GrimArms old(1, 0);
    heurarm::t_arm[heurarm::BLOODSUN_GRIM_V2] = 1;
    GameState s = BasePf(3, 20);
    s.battlefield.erase(s.battlefield.begin());   // Blood Sun is held, no red source yet
    s.players[0].hand.push_back(CardPf("Blood Sun", 40));
    s.players[0].hand.push_back(CardPf("Grim Tutor", 41));
    s.players[0].library.push_back(CardPf("City of Brass", 500));
    s.players[0].library.push_back(CardPf("Diabolic Revelation", 501));
    s.players[0].library.push_back(CardPf("Scorched Ruins", 502));
    const CardParams& gp = DefPf("Grim Tutor").params;
    CHECK(HasPf(ProvPf().TutorCandidates(s, 0, gp), "City of Brass"));
    heurarm::t_arm[heurarm::BLOODSUN_GRIM_V2_RED_COND] = 1;
    CHECK_FALSE(HasPf(ProvPf().TutorCandidates(s, 0, gp), "City of Brass"));
    s.players[0].hand.push_back(CardPf("Diabolic Revelation", 42));
    CHECK(HasPf(ProvPf().TutorCandidates(s, 0, gp), "City of Brass"));
    heurarm::t_arm[heurarm::BLOODSUN_GRIM_V2_RED_OFF] = 1;
    CHECK_FALSE(HasPf(ProvPf().TutorCandidates(s, 0, gp), "City of Brass"));
    heurarm::t_arm[heurarm::BLOODSUN_GRIM_V2_RED_OFF] = -1;
    heurarm::t_arm[heurarm::BLOODSUN_GRIM_V2_RED_COND] = -1;
    s = BasePf(3, 20);
    s.players[0].hand.push_back(CardPf("Blood Sun", 44));
    s.players[0].hand.push_back(CardPf("Grim Tutor", 45));
    s.players[0].library.push_back(CardPf("City of Brass", 505));
    CHECK_FALSE(HasPf(ProvPf().TutorCandidates(s, 0, gp), "City of Brass"));   // Sun already out
    s = BasePf(3, 20);   // Sun out, Revelation missing: without ONLY_MISSING, the one-land row also fires
    s.players[0].hand.push_back(CardPf("Grim Tutor", 43));
    s.players[0].library.push_back(CardPf("Diabolic Revelation", 503));
    s.players[0].library.push_back(CardPf("Scorched Ruins", 504));
    CHECK(HasPf(ProvPf().TutorCandidates(s, 0, gp), "Scorched Ruins"));
    heurarm::t_arm[heurarm::BLOODSUN_GRIM_V2_ONLY_MISSING] = 1;
    const auto only = ProvPf().TutorCandidates(s, 0, gp);
    CHECK(only.size() == 1);
    CHECK(only[0] == "Diabolic Revelation");
    heurarm::t_arm[heurarm::BLOODSUN_GRIM_V2_ONLY_MISSING] = -1;
    heurarm::t_arm[heurarm::BLOODSUN_GRIM_V2] = -1;
}

namespace
{
struct LotusArm
{
    explicit LotusArm(int on)
    {
        heurarm::t_arm[heurarm::BLOODSUN_USER_PRUNE]      = 1;
        heurarm::t_arm[heurarm::BLOODSUN_LOTUS_SAC_PRUNE] = static_cast<std::int8_t>(on);
    }
    ~LotusArm()
    {
        heurarm::t_arm[heurarm::BLOODSUN_USER_PRUNE]      = -1;
        heurarm::t_arm[heurarm::BLOODSUN_LOTUS_SAC_PRUNE] = -1;
    }
};
// No Blood Sun (the only case with a Lotus sacrifice): our lands as named, all untapped.
GameState NoSunPf(const std::vector<std::string>& lands)
{
    GameState s = BasePf(0, 20);
    s.battlefield.clear();
    for (std::size_t i = 0; i < lands.size(); ++i) { PutPf(s, lands[i], 100 + static_cast<int>(i)); }
    return s;
}
}   // namespace

TEST_CASE("Blood Sun B13: the Lotus Vale sacrifice collapses to one pair -- Karoo kept, then R, G and two B (on / off)")
{
    GameState s = NoSunPf({ "Swamp", "Swamp", "Swamp", "Mountain", "Forest", "Rakdos Carnarium" });
    {
        LotusArm arm(0);
        CHECK(LotusSacrificePicks(s, 0, DefPf("Lotus Vale"), -1).size() > 1);   // the full multiset menu
    }
    LotusArm arm(1);
    const std::vector<std::vector<int>> picks = LotusSacrificePicks(s, 0, DefPf("Lotus Vale"), -1);
    REQUIRE(picks.size() == 1);
    CHECK(LotusSacrificePickCount(s, 0, DefPf("Lotus Vale")) == 1);
    int r = 0, g = 0, b = 0;
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    {
        const std::string& nm = s.battlefield[static_cast<std::size_t>(i)].card.m_name.str();
        const bool victim = std::find(picks[0].begin(), picks[0].end(), i) != picks[0].end();
        if (victim) { CHECK(nm != "Rakdos Carnarium"); continue; }   // (2) the Karoo is kept
        if (nm == "Mountain" || nm == "Rakdos Carnarium") { ++r; }
        if (nm == "Forest") { ++g; }
        if (nm == "Swamp" || nm == "Rakdos Carnarium") { ++b; }
    }
    CHECK(r >= 1);   // (1)
    CHECK(g >= 1);
    CHECK(b >= 2);
}

TEST_CASE("Blood Sun B13: Lotus Field takes the tapped twin before the untapped one (F-L1)")
{
    // Field (trigger: any land, itself included). Lands: Forest, Swamp (tapped), Swamp, the Field. Every pair leaves
    // the same colour shortfall (a kept Field is ONE any-colour source), so (3) decides: never the untapped Swamp
    // while its tapped twin is still there.
    GameState s = NoSunPf({ "Forest", "Swamp", "Swamp", "Lotus Field" });
    s.battlefield[1].tapped = true;
    LotusArm arm(1);
    const std::vector<std::vector<int>> picks = LotusSacrificePicks(s, 0, DefPf("Lotus Field"), 3);
    REQUIRE(picks.size() == 1);
    const std::vector<int>& v = picks[0];
    // Never the untapped Swamp while its tapped twin is KEPT (sacrificing both Swamps ties on (1)-(3) and is allowed).
    const bool untapped_gone = std::find(v.begin(), v.end(), 2) != v.end();
    const bool tapped_gone   = std::find(v.begin(), v.end(), 1) != v.end();
    CHECK(!(untapped_gone && !tapped_gone));
}

// ---- Blood Sun perf part 3: the cast-trigger target fan held lazily (MTG_BLOODSUN_LAZY_CTT) -------------------------
// The lever's whole claim is an IDENTITY of the candidate list: the scorer, indexing the lazy view, reads exactly the
// plans the eager axes build, in the same order. So each case builds one node's base plans, runs the axes eagerly
// (SubdecisionAxesForTest) and lazily (LazySubdecisionAxesViewForTest, as SolveWithLookahead requests the list), and
// compares every index with Plan::operator== (every member).
namespace
{
GameState UlamogBoardPf()
{
    GameState s = BasePf(3, 20);
    s.deck_has_cast_target_trigger = true;
    // GameState's deck_has_dredge DEFAULTS TO TRUE (a missing stamp must not lose the axis); the runner clears it for
    // a deck with no dredger (GoldFishRunner: garth || dredge), Blood Sun included. Left set, the dredge axis
    // materialises every lazy layout (it replays each plan), which is the eager list again: correct, but the lazy
    // count below would read 0 for a reason real Blood Sun play never meets.
    s.deck_has_dredge = false;
    PutPf(s, "Forest", 200);
    PutPf(s, "Island", 201);
    PutPf(s, "Mountain", 202);
    return s;
}
std::vector<TurnSolver::Plan> UlamogPlansPf()
{
    std::vector<TurnSolver::Plan> plans(3);
    plans[0].actions = { CastPf("Ulamog, the Ceaseless Hunger") };
    plans[0].value   = 900;
    plans[1].actions = {};                                   // the pass: no Ulamog, no fan
    plans[1].value   = 10;
    plans[2].actions = { CastPf("Ulamog, the Ceaseless Hunger"), CastPf("Time Warp") };
    plans[2].value   = 950;
    for (TurnSolver::Plan& p : plans) { p.land_decided = true; }
    return plans;
}
void CheckSameListPf(const std::vector<TurnSolver::Plan>& eager, const std::vector<TurnSolver::Plan>& lazy)
{
    REQUIRE(eager.size() == lazy.size());
    for (std::size_t i = 0; i < eager.size(); ++i) { CHECK_MESSAGE(eager[i] == lazy[i], "index ", i); }
}
}   // namespace

TEST_CASE("Blood Sun lazy cast-trigger fan: the lazy view is the eager list, index for index (no inherited pin)")
{
    const GameState s = UlamogBoardPf();
    const std::vector<TurnSolver::Plan> eager = TurnSolver::SubdecisionAxesForTest(s, true, UlamogPlansPf());
    std::size_t lazy_n = 0;
    const std::vector<TurnSolver::Plan> lazy = TurnSolver::LazySubdecisionAxesViewForTest(s, true, UlamogPlansPf(), lazy_n);
    // Both Ulamog plans fanned: the list holds clones beyond the three base plans...
    std::size_t clones = 0;
    for (const TurnSolver::Plan& p : eager) { clones += p.cast_trigger_target_choice >= 0 ? 1 : 0; }
    REQUIRE(clones > 2);
    // ...and with the lever on (the default) every one of them was held lazily, none built by the axes.
    CHECK(lazy_n == (BloodSunProvider().LazyCastTriggerTargetFan() ? clones : 0));
    CheckSameListPf(eager, lazy);
}

TEST_CASE("Blood Sun lazy cast-trigger fan: an inherited pin is skipped exactly as the eager ranks skip it")
{
    const GameState s = UlamogBoardPf();
    const ScriptedCastTriggerTarget pin(2, s.TurnSeq());   // a main-1 pin standing when this list is built
    const std::vector<TurnSolver::Plan> eager = TurnSolver::SubdecisionAxesForTest(s, true, UlamogPlansPf());
    std::size_t lazy_n = 0;
    const std::vector<TurnSolver::Plan> lazy = TurnSolver::LazySubdecisionAxesViewForTest(s, true, UlamogPlansPf(), lazy_n);
    CheckSameListPf(eager, lazy);
    bool saw0 = false, saw2 = false;
    for (const TurnSolver::Plan& p : lazy)
    {
        saw0 = saw0 || p.cast_trigger_target_choice == 0;
        saw2 = saw2 || p.cast_trigger_target_choice == 2;
    }
    CHECK(saw0);    // rank 0 is a variant now (the inherited pin is the base's)...
    CHECK(!saw2);   // ...and the inherited rank is not repeated
}

TEST_CASE("Blood Sun lazy cast-trigger fan: another deck's provider keeps the eager fan (byte-identical by construction)")
{
    GameState s = UlamogBoardPf();
    s.m_provider = nullptr;   // the generic provider: LazyCastTriggerTargetFan() == false
    const std::vector<TurnSolver::Plan> eager = TurnSolver::SubdecisionAxesForTest(s, true, UlamogPlansPf());
    std::size_t lazy_n = 0;
    const std::vector<TurnSolver::Plan> lazy = TurnSolver::LazySubdecisionAxesViewForTest(s, true, UlamogPlansPf(), lazy_n);
    CHECK(lazy_n == 0);
    CheckSameListPf(eager, lazy);
}

// ---- Blood Sun perf part 6: the Grim prune's three AMENDMENT ARMS (DEFAULT OFF; the USER's call). Fixtures are the
// boards of the four d8 b0 games the prune lost (gi 7 / 277 DROP, gi 141 BOARD, gi 431 TWNEXT).
namespace
{
struct GrimAmend
{
    GrimAmend(int drop, int board, int tw)
    {
        heurarm::t_arm[heurarm::BLOODSUN_USER_PRUNE]      = 1;
        heurarm::t_arm[heurarm::BLOODSUN_GRIM_PRUNE]      = 1;
        heurarm::t_arm[heurarm::BLOODSUN_GRIM_REV_GATE]   = 0;
        heurarm::t_arm[heurarm::BLOODSUN_GRIM_LAND_DROP]  = static_cast<std::int8_t>(drop);
        heurarm::t_arm[heurarm::BLOODSUN_GRIM_LAND_BOARD] = static_cast<std::int8_t>(board);
        heurarm::t_arm[heurarm::BLOODSUN_GRIM_TW_ANY]     = static_cast<std::int8_t>(tw);
    }
    ~GrimAmend()
    {
        for (int k : { heurarm::BLOODSUN_USER_PRUNE, heurarm::BLOODSUN_GRIM_PRUNE, heurarm::BLOODSUN_GRIM_REV_GATE,
                       heurarm::BLOODSUN_GRIM_LAND_DROP, heurarm::BLOODSUN_GRIM_LAND_BOARD, heurarm::BLOODSUN_GRIM_TW_ANY })
        { heurarm::t_arm[k] = -1; }
    }
};
// Blood Sun out, `swamps` untapped Swamps, Grim Tutor in hand, a library holding every B3 target Grim can name here.
GameState GrimBoardPf(int swamps)
{
    GameState s = BasePf(swamps, 20);
    s.players[0].hand.push_back(CardPf("Grim Tutor", 40));
    for (const char* n : { "Diabolic Revelation", "Time Warp", "Scorched Ruins", "Lotus Vale" })
    { s.players[0].library.push_back(CardPf(n, 500 + static_cast<int>(s.players[0].library.size()))); }
    s.players[0].hand.push_back(CardPf("Diabolic Revelation", 41));   // Revelation not missing
    return s;
}
std::vector<std::string> GrimTargetsPf(const GameState& s)
{
    return ProvPf().TutorCandidates(s, 0, DefPf("Grim Tutor").params);
}
}   // namespace

TEST_CASE("Blood Sun Grim amendment DROP: a big land at 7+ mana only while a land play is open this turn")
{
    // gi 277 / gi 7: 7+ land mana, no land in hand, the drop (or Fastbond's) still open.
    GameState s = GrimBoardPf(7);
    {
        GrimAmend a(0, 0, 0);   // the B12 list: 7 mana closes the big lands
        CHECK_FALSE(HasPf(GrimTargetsPf(s), "Scorched Ruins"));
    }
    GrimAmend a(1, 0, 0);
    CHECK(HasPf(GrimTargetsPf(s), "Scorched Ruins"));
    CHECK(HasPf(GrimTargetsPf(s), "Lotus Vale"));
    s.players[0].lands_played_this_turn = 1;   // the drop is used: closed again
    CHECK_FALSE(HasPf(GrimTargetsPf(s), "Scorched Ruins"));
    s.players[0].bonus_land_drops_this_turn = 1;   // Exploration's (or Fastbond's) extra drop: open
    CHECK(HasPf(GrimTargetsPf(s), "Scorched Ruins"));
    s.active_player_index = 1;   // not our turn: no land play of ours this turn
    CHECK_FALSE(BloodSunGrimTargetAdmitted(s, 0, DefPf("Scorched Ruins")));
}

TEST_CASE("Blood Sun Grim amendment BOARD: the 7-mana test counts battlefield lands only")
{
    // gi 141: Blood Sun out, 6 mana of lands on the battlefield (3 Swamps + Lotus Vale), Golgari Rot Farm (2) in hand.
    GameState s = GrimBoardPf(3);
    PutPf(s, "Lotus Vale", 120);
    s.players[0].hand.push_back(CardPf("Golgari Rot Farm", 42));
    s.players[0].lands_played_this_turn = 2;
    s.players[0].bonus_land_drops_this_turn = 1;   // both drops used
    {
        GrimAmend a(0, 0, 0);   // 6 + the Karoo's 2 = 8 >= 7: closed
        CHECK_FALSE(HasPf(GrimTargetsPf(s), "Scorched Ruins"));
    }
    {
        GrimAmend a(1, 0, 0);   // DROP alone: no drop is open, still closed
        CHECK_FALSE(HasPf(GrimTargetsPf(s), "Scorched Ruins"));
    }
    GrimAmend a(0, 1, 0);
    CHECK(HasPf(GrimTargetsPf(s), "Scorched Ruins"));
    PutPf(s, "Swamp", 121);   // 7 on the battlefield: closed under BOARD too
    CHECK_FALSE(HasPf(GrimTargetsPf(s), "Scorched Ruins"));
}

TEST_CASE("Blood Sun Grim amendment TWNEXT: Time Warp whenever none is in hand")
{
    // gi 431: 4 lands, no blue -- Grim + Time Warp is not castable this turn, so B12 drops Time Warp.
    GameState s = GrimBoardPf(4);
    {
        GrimAmend a(0, 0, 0);
        CHECK_FALSE(HasPf(GrimTargetsPf(s), "Time Warp"));
    }
    GrimAmend a(0, 0, 1);
    CHECK(HasPf(GrimTargetsPf(s), "Time Warp"));
    s.players[0].hand.push_back(CardPf("Time Warp", 43));   // one in hand already: not a target
    CHECK_FALSE(HasPf(GrimTargetsPf(s), "Time Warp"));
}

TEST_CASE("Blood Sun Grim amendments are inert while MTG_BLOODSUN_GRIM_PRUNE is off")
{
    GameState s = GrimBoardPf(7);
    GrimAmend a(1, 1, 1);
    heurarm::t_arm[heurarm::BLOODSUN_GRIM_PRUNE] = 0;   // the B3 list: every big land and Time Warp, as before
    const std::vector<std::string> t = GrimTargetsPf(s);
    CHECK(HasPf(t, "Scorched Ruins"));
    CHECK(HasPf(t, "Time Warp"));
}

TEST_CASE("Blood Sun Grim amendment LATE: amendment-only targets rank after the base B12 targets, none refused")
{
    GameState s = GrimBoardPf(7);
    s.players[0].hand.pop_back();   // no Revelation in hand: Revelation is a base B12 target
    GrimAmend a(1, 0, 0);
    {
        const std::vector<std::string> t = GrimTargetsPf(s);
        REQUIRE(!t.empty());
        CHECK(t.front() != "Diabolic Revelation");   // BloodSunRole: the big lands first
        CHECK(HasPf(t, "Scorched Ruins"));
    }
    heurarm::t_arm[heurarm::BLOODSUN_GRIM_AMEND_LATE] = 1;
    const std::vector<std::string> t = GrimTargetsPf(s);
    heurarm::t_arm[heurarm::BLOODSUN_GRIM_AMEND_LATE] = -1;
    REQUIRE(!t.empty());
    CHECK(t.front() == "Diabolic Revelation");   // the base target first, the DROP-only lands after it
    CHECK(HasPf(t, "Scorched Ruins"));
    CHECK(HasPf(t, "Lotus Vale"));
}
