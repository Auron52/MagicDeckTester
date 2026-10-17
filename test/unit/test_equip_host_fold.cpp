// Unit tests for MTG_EQUIP_HOST_FOLD -- interchangeable Equip / reconfigure HOSTS (TurnSolver.cpp,
// BuildEquipHostFold + SubsetHasNonCanonicalEquipHost).
//
// The fold drops a subset only when it is the MIRROR IMAGE of a canonical one: the same equips with the
// identical hosts relabelled. So the tests check the two halves of that claim directly, on the board the
// Colossus Hammer sweep found it on (two Speakers of the Heavens):
//   * NOTHING IS LOST: every plan of the unfolded menu has a plan in the folded menu that is the same up
//     to swapping the two Speakers -- including "one on each", which a keep-one-host cut would delete;
//   * IT IS AN IDENTITY: a dropped plan and its surviving twin realise the same board up to that swap, in
//     the rollout AND in the executor;
// and the refusals: a Speaker that differs (damage, summoning sickness, a counter, what it wears), a
// candidate that names a Speaker, and a Sigarda's Aid that would pick among them.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace
{

void EnsureCardsHf()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(
            ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefHf(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

constexpr int kS1 = 50, kS2 = 51;   // the two Speakers

GameState BoardHf()
{
    EnsureCardsHf();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;   // land drop spent: no land-fold plan variants
    for (int k = 0; k < 20; ++k)
    {
        Card c = DefHf("Plains").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    auto put = [&](const std::string& name, int number)
    {
        Permanent p;
        p.card             = DefHf(name).card;
        p.card.m_number    = number;
        p.controller_index = 0;
        p.owner_index      = 0;
        s.battlefield.push_back(p);
    };
    put("Forge Anew", 10);           // the Hammer's equip is free (the first-equip waiver)
    put("Colossus Hammer", 4);       // loose
    put("Kor Duelist", 15);
    put("Lizard Blades", 19);        // loose: a creature, and a reconfigure source
    put("Speaker of the Heavens", kS1);
    put("Speaker of the Heavens", kS2);
    for (int k = 0; k < 4; ++k) { put("Plains", 36 + k); }
    return s;
}

Permanent& ByNumHf(GameState& s, int number)
{
    for (Permanent& p : s.battlefield) { if (p.card.m_number == number) { return p; } }
    static Permanent none;
    REQUIRE_MESSAGE(false, "permanent not found: ", number);
    return none;
}

int Swap(int n) { return n == kS1 ? kS2 : n == kS2 ? kS1 : n; }

// One plan as a sorted list of action signatures, optionally with the two Speakers swapped.
std::string Sig(const TurnSolver::Plan& p, bool swapped)
{
    std::vector<std::string> v;
    for (const Action& a : p.actions)
    {
        auto f = [&](int n) { return swapped ? Swap(n) : n; };
        v.push_back(std::to_string(static_cast<int>(a.kind)) + ":" + a.card_name.str() + ":"
                    + std::to_string(f(a.sac_source_id)) + ">" + std::to_string(f(a.sac_victim_id)) + "/e"
                    + std::to_string(f(a.enchant_target)) + "/m" + std::to_string(f(a.attach_equip_id))
                    + "/t" + a.tutor_target.str());
    }
    std::sort(v.begin(), v.end());
    std::string k = static_cast<const std::string&>(p.land_to_play) + "|";
    for (const std::string& x : v) { k += x + ";"; }
    return k;
}
std::string Orbit(const TurnSolver::Plan& p) { return std::min(Sig(p, false), Sig(p, true)); }

std::vector<TurnSolver::Plan> Menu(const GameState& s, int fold)
{
    heurarm::t_arm[heurarm::EQUIP_HOST_FOLD] = static_cast<std::int8_t>(fold);
    std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    heurarm::t_arm[heurarm::EQUIP_HOST_FOLD] = -1;
    return plans;
}

// Did the fold drop anything, and is every unfolded orbit still represented?
struct FoldResult { std::size_t off = 0, on = 0; bool every_orbit = true; bool subset = true; };
FoldResult Compare(const GameState& s)
{
    const std::vector<TurnSolver::Plan> off = Menu(s, 0), on = Menu(s, 1);
    std::set<std::string> on_exact, on_orbit;
    for (const TurnSolver::Plan& p : on) { on_exact.insert(Sig(p, false)); on_orbit.insert(Orbit(p)); }
    std::set<std::string> off_exact;
    for (const TurnSolver::Plan& p : off) { off_exact.insert(Sig(p, false)); }
    FoldResult r;
    r.off = off_exact.size();
    r.on  = on_exact.size();
    for (const TurnSolver::Plan& p : off) { if (!on_orbit.count(Orbit(p))) { r.every_orbit = false; } }
    for (const std::string& k : on_exact) { if (!off_exact.count(k)) { r.subset = false; } }
    return r;
}

int CountOnto(const TurnSolver::Plan& p, int host)
{
    int n = 0;
    for (const Action& a : p.actions) { if (a.kind == Action::Kind::Equip && a.sac_victim_id == host) { ++n; } }
    return n;
}

void ResolveAllHf(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

GameState RollHf(const GameState& s, const TurnSolver::Plan& p)
{
    GameState t = s;
    TurnSolver::ApplyPlan(t, p, /*is_pre_combat=*/true);
    return t;
}
GameState ExecHf(const GameState& s, const TurnSolver::Plan& p)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, p, true);
    GameState t = s;
    eng.TakeTurn(t, true, ResolveAllHf);
    return t;
}

// A board, optionally with the Speakers swapped, as a comparable string.
std::string BoardKey(const GameState& g, bool swapped)
{
    std::vector<std::string> v;
    for (const Permanent& p : g.battlefield)
    {
        auto f = [&](int n) { return swapped ? Swap(n) : n; };
        v.push_back(std::to_string(f(p.card.m_number)) + "@" + std::to_string(f(p.equipped_to)) + (p.tapped ? "T" : "u")
                    + (p.card.IsCreature() ? "c" : "n"));
    }
    std::sort(v.begin(), v.end());
    std::string k;
    for (const std::string& x : v) { k += x + ";"; }
    k += "|hand" + std::to_string(g.players[0].hand.size()) + "|gy" + std::to_string(g.players[0].graveyard.size())
       + "|w" + std::to_string(g.players[0].equip_activated_this_turn ? 1 : 0);
    return k;
}

}   // namespace

TEST_CASE("Equip host fold: two identical Speakers -- only mirror images are dropped, every line survives")
{
    const GameState s = BoardHf();
    const FoldResult r = Compare(s);
    INFO("unfolded " << r.off << " plans, folded " << r.on);
    MESSAGE("two-Speaker frame (autonomous menu): " << r.off << " distinct plans unfolded -> " << r.on << " folded");
    CHECK(r.on < r.off);          // it fires
    CHECK(r.subset);              // it only removes
    CHECK(r.every_orbit);         // and never a whole class of mirror images
    // The spread line (one piece on each Speaker) survives -- what a keep-one-host cut would delete.
    bool spread = false;
    for (const TurnSolver::Plan& p : Menu(s, 1)) { if (CountOnto(p, kS1) == 1 && CountOnto(p, kS2) == 1) { spread = true; } }
    CHECK(spread);
    // The canonical form: a folded-menu plan never uses the SECOND Speaker unless an earlier source
    // (by card number) already used the first.
    for (const TurnSolver::Plan& p : Menu(s, 1))
    {
        std::vector<std::pair<int, int>> uses;   // (source, host) onto a Speaker
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::Equip && (a.sac_victim_id == kS1 || a.sac_victim_id == kS2))
            { uses.emplace_back(a.sac_source_id, a.sac_victim_id); }
        }
        std::sort(uses.begin(), uses.end());
        if (!uses.empty()) { CHECK(uses.front().second == kS1); }
    }
}

TEST_CASE("Equip host fold: a dropped plan and its twin realise the same board up to the swap, in BOTH worlds")
{
    const GameState s = BoardHf();
    const std::vector<TurnSolver::Plan> off = Menu(s, 0), on = Menu(s, 1);
    std::set<std::string> on_exact;
    for (const TurnSolver::Plan& p : on) { on_exact.insert(Sig(p, false)); }
    int checked = 0;
    for (const TurnSolver::Plan& d : off)
    {
        if (on_exact.count(Sig(d, false))) { continue; }            // not dropped
        const TurnSolver::Plan* twin = nullptr;
        for (const TurnSolver::Plan& p : on) { if (Sig(p, false) == Sig(d, true)) { twin = &p; break; } }
        REQUIRE_MESSAGE(twin != nullptr, "a dropped plan has no mirror twin: ", Sig(d, false));
        for (int world = 0; world < 2; ++world)
        {
            INFO((world == 0 ? "rollout" : "executor") << " " << Sig(d, false));
            const GameState a = world == 0 ? RollHf(s, d) : ExecHf(s, d);
            const GameState b = world == 0 ? RollHf(s, *twin) : ExecHf(s, *twin);
            CHECK(BoardKey(a, true) == BoardKey(b, false));
        }
        ++checked;
    }
    CHECK(checked > 0);
}

TEST_CASE("Equip host fold: executor and rollout realise every folded-menu plan identically (two identical hosts)")
{
    const GameState s = BoardHf();
    int checked = 0;
    for (const TurnSolver::Plan& p : Menu(s, 1))
    {
        INFO(Sig(p, false));
        CHECK(BoardKey(RollHf(s, p), false) == BoardKey(ExecHf(s, p), false));
        ++checked;
    }
    CHECK(checked > 5);
}

TEST_CASE("Equip host fold: refused when the Speakers differ, or something reads one of them")
{
    auto unfolded = [](const GameState& s)
    {
        const FoldResult r = Compare(s);
        INFO("unfolded " << r.off << " plans, folded " << r.on);
        CHECK(r.every_orbit);
        return r.on == r.off;
    };
    {   // damage already marked on one
        GameState s = BoardHf(); ByNumHf(s, kS2).damage = 1;
        CHECK(unfolded(s));
    }
    {   // summoning sick
        GameState s = BoardHf(); ByNumHf(s, kS2).entered_this_turn = true;
        CHECK(unfolded(s));
    }
    {   // a +1/+1 counter
        GameState s = BoardHf(); ByNumHf(s, kS2).counters.Add(Counter::Type::PlusOnePlusOne, 1);
        CHECK(unfolded(s));
    }
    {   // tapped
        GameState s = BoardHf(); ByNumHf(s, kS2).tapped = true;
        CHECK(unfolded(s));
    }
    {   // wearing something (both wearing nothing is the only symmetric case the fold accepts)
        GameState s = BoardHf();
        ApplyEquip(s, 0, 19, kS2);   // the Lizard on Speaker #2
        CHECK(unfolded(s));
    }
    {   // a Theft in hand whose creature target is a Speaker: its plans are never folded
        GameState s = BoardHf();
        Card th = DefHf("Magnetic Theft").card; th.m_number = 60; s.players[0].hand.push_back(th);
        Permanent m; m.card = DefHf("Mountain").card; m.card.m_number = 40; m.controller_index = 0; m.owner_index = 0;
        s.battlefield.push_back(m);
        const std::vector<TurnSolver::Plan> off = Menu(s, 0), on = Menu(s, 1);
        auto theft_onto_speaker = [](const TurnSolver::Plan& p)
        {
            for (const Action& a : p.actions)
            { if (a.attach_equip_id != 0 && (a.enchant_target == kS1 || a.enchant_target == kS2)) { return true; } }
            return false;
        };
        std::size_t n_off = 0, n_on = 0;
        for (const TurnSolver::Plan& p : off) { if (theft_onto_speaker(p)) { ++n_off; } }
        for (const TurnSolver::Plan& p : on)  { if (theft_onto_speaker(p)) { ++n_on; } }
        CHECK(n_off > 0);
        CHECK(n_on == n_off);
        CHECK(Compare(s).every_orbit);
    }
    {   // Sigarda's Aid out and a Hammer cast: the Aid would PICK a Speaker on entry -- never folded
        GameState s = BoardHf();
        Permanent aid; aid.card = DefHf("Sigarda's Aid").card; aid.card.m_number = 46; aid.controller_index = 0; aid.owner_index = 0;
        s.battlefield.push_back(aid);
        Card ch = DefHf("Colossus Hammer").card; ch.m_number = 5; s.players[0].hand.push_back(ch);
        const std::vector<TurnSolver::Plan> off = Menu(s, 0), on = Menu(s, 1);
        auto casts_hammer = [](const TurnSolver::Plan& p)
        {
            for (const Action& a : p.actions)
            { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Colossus Hammer") { return true; } }
            return false;
        };
        std::size_t n_off = 0, n_on = 0;
        for (const TurnSolver::Plan& p : off) { if (casts_hammer(p)) { ++n_off; } }
        for (const TurnSolver::Plan& p : on)  { if (casts_hammer(p)) { ++n_on; } }
        CHECK(n_off > 0);
        CHECK(n_on == n_off);
        CHECK(Compare(s).every_orbit);
    }
}

TEST_CASE("Equip host fold: the claude-play sweep's seed-2100003 T5 board, autonomous menu before / after")
{
    // Forge Anew, Furycalm Snarl + 3 Plains, Kor Duelist, a loose Lizard Blades, two Speakers of the
    // Heavens, a Swiftblade Vindicator; Colossus Hammer and Magnetic Theft in hand.
    EnsureCardsHf();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life     = 21;
    s.players[1].life     = 10;
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < 20; ++k) { Card c = DefHf("Plains").card; c.m_number = 900 + k; s.players[0].library.push_back(c); }
    auto put = [&](const std::string& name, int number)
    {
        Permanent p; p.card = DefHf(name).card; p.card.m_number = number; p.controller_index = 0; p.owner_index = 0;
        s.battlefield.push_back(p);
    };
    put("Furycalm Snarl", 11); put("Kor Duelist", 15); put("Plains", 36); put("Swiftblade Vindicator", 57);
    put("Plains", 37); put("Lizard Blades", 19); put("Speaker of the Heavens", kS1); put("Plains", 38);
    put("Forge Anew", 10); put("Speaker of the Heavens", kS2);
    Card ch = DefHf("Colossus Hammer").card; ch.m_number = 5;  s.players[0].hand.push_back(ch);
    Card th = DefHf("Magnetic Theft").card;  th.m_number = 22; s.players[0].hand.push_back(th);
    const FoldResult r = Compare(s);
    MESSAGE("seed-2100003 T5 board (autonomous menu): " << r.off << " distinct plans unfolded -> " << r.on << " folded");
    CHECK(r.subset);
    CHECK(r.every_orbit);
    CHECK(r.on <= r.off);
}
