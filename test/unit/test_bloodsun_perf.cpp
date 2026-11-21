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
