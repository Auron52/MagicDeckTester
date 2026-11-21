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
}   // namespace

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

TEST_CASE("Blood Sun go-off front: ORDER only, off under MTG_BLOODSUN_GOOFF's human-play guard")
{
    GameState s = BasePf(25, 15);
    const std::vector<Action> cands = { CastPf("Emrakul, the Aeons Torn") };
    CHECK(ProvPf().PlanMoveOrderFront(s, cands, { 0 }, false) == BloodSunGoOffOn());
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
    CHECK(std::find(v.begin(), v.end(), 2) == v.end());   // never the untapped Swamp before the tapped one
}
