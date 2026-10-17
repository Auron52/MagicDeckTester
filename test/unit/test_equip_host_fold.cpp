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
    {   // a Speaker's own tap names it: a subset holding that activation is never folded. (A Theft onto a
        // Speaker is no longer a refusal -- it joins the canonical form; see the Theft cases below.)
        GameState s = BoardHf();
        s.players[0].life = gamesetup::StartingLife() + 7;     // Speaker of the Heavens' life gate
        const std::vector<TurnSolver::Plan> off = Menu(s, 0), on = Menu(s, 1);
        auto taps_speaker = [](const TurnSolver::Plan& p)
        {
            for (const Action& a : p.actions)
            {
                if (a.kind != Action::Kind::Equip && a.kind != Action::Kind::CastFromHand
                    && (a.sac_source_id == kS1 || a.sac_source_id == kS2)) { return true; }
            }
            return false;
        };
        std::size_t n_off = 0, n_on = 0;
        for (const TurnSolver::Plan& p : off) { if (taps_speaker(p)) { ++n_off; } }
        for (const TurnSolver::Plan& p : on)  { if (taps_speaker(p)) { ++n_on; } }
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

// ---- Magnetic Theft joins the canonical form (2026-10-17) ------------------------------------------
// Until then the Theft's pair axis kept ONE creature and ONE Equipment per identity class, so two Thefts
// could never put one Equipment on EACH of two identical creatures, nor move each of two identical
// pieces -- unfindable at any budget. Now every pair is offered and the mirror images are folded jointly
// with the Equips.
namespace
{
// A relabelling: swap the two Speakers and/or the two identical Hammers.
struct Relabel { bool hosts = false; bool pieces = false; };
constexpr int kH1 = 4, kH2 = 6;   // two identical loose Colossus Hammers
int Map(int n, Relabel r)
{
    if (r.hosts)  { n = Swap(n); }
    if (r.pieces) { n = n == kH1 ? kH2 : n == kH2 ? kH1 : n; }
    return n;
}
std::string SigR(const TurnSolver::Plan& p, Relabel r)
{
    std::vector<std::string> v;
    for (const Action& a : p.actions)
    {
        // No hand_index: two Theft CARDS are identical, and the plan dedup keys a Theft by its pair.
        v.push_back(std::to_string(static_cast<int>(a.kind)) + ":" + a.card_name.str() + ":"
                    + std::to_string(Map(a.sac_source_id, r)) + ">"
                    + std::to_string(Map(a.sac_victim_id, r)) + "/e" + std::to_string(Map(a.enchant_target, r))
                    + "/m" + std::to_string(Map(a.attach_equip_id, r)) + "/t" + a.tutor_target.str());
    }
    std::sort(v.begin(), v.end());
    std::string k = static_cast<const std::string&>(p.land_to_play) + "|";
    for (const std::string& x : v) { k += x + ";"; }
    return k;
}
std::string OrbitR(const TurnSolver::Plan& p)
{
    std::string best = SigR(p, {});
    for (Relabel r : { Relabel{ true, false }, Relabel{ false, true }, Relabel{ true, true } })
    { best = std::min(best, SigR(p, r)); }
    return best;
}
std::string BoardKeyR(const GameState& g, Relabel r)
{
    std::vector<std::string> v;
    for (const Permanent& p : g.battlefield)
    {
        v.push_back(std::to_string(Map(p.card.m_number, r)) + "@" + std::to_string(Map(p.equipped_to, r))
                    + (p.tapped ? "T" : "u") + (p.card.IsCreature() ? "c" : "n"));
    }
    std::sort(v.begin(), v.end());
    std::string k;
    for (const std::string& x : v) { k += x + ";"; }
    k += "|hand" + std::to_string(g.players[0].hand.size()) + "|gy" + std::to_string(g.players[0].graveyard.size())
       + "|w" + std::to_string(g.players[0].equip_activated_this_turn ? 1 : 0);
    return k;
}
// Two Speakers, a Kor Duelist, a loose Lizard Blades, two identical loose Hammers, a Loxodon
// Warhammer, two Magnetic Thefts in hand, four Mountains. Forge Anew keeps a Hammer equip in reach.
GameState TheftBoard()
{
    GameState s = BoardHf();        // Forge Anew, Hammer #4, Kor Duelist, Lizard, Speakers 50/51, 4 Plains
    auto put = [&](const std::string& name, int number)
    {
        Permanent p; p.card = DefHf(name).card; p.card.m_number = number; p.controller_index = 0; p.owner_index = 0;
        s.battlefield.push_back(p);
    };
    put("Colossus Hammer", kH2);
    put("Loxodon Warhammer", 7);
    for (int k = 0; k < 4; ++k) { put("Mountain", 70 + k); }
    Card t1 = DefHf("Magnetic Theft").card; t1.m_number = 60; s.players[0].hand.push_back(t1);
    Card t2 = DefHf("Magnetic Theft").card; t2.m_number = 62; s.players[0].hand.push_back(t2);
    return s;
}
bool IsTheftHf(const Action& a) { return a.kind == Action::Kind::CastFromHand && a.attach_equip_id != 0; }
}   // namespace

TEST_CASE("Equip host fold / Theft: two Thefts can now put one Equipment on EACH of two identical Speakers")
{
    const GameState s = TheftBoard();
    auto spread_hosts = [](const TurnSolver::Plan& p)
    {
        int on1 = 0, on2 = 0;
        for (const Action& a : p.actions)
        {
            if (!IsTheftHf(a)) { continue; }
            if (a.enchant_target == kS1) { ++on1; }
            if (a.enchant_target == kS2) { ++on2; }
        }
        return on1 == 1 && on2 == 1;
    };
    auto spread_pieces = [](const TurnSolver::Plan& p)
    {
        int m1 = 0, m2 = 0;
        for (const Action& a : p.actions)
        {
            if (!IsTheftHf(a)) { continue; }
            if (a.attach_equip_id == kH1) { ++m1; }
            if (a.attach_equip_id == kH2) { ++m2; }
        }
        return m1 == 1 && m2 == 1;
    };
    for (int fold = 0; fold < 2; ++fold)
    {
        INFO("fold " << fold);
        bool sh = false, sp = false;
        for (const TurnSolver::Plan& p : Menu(s, fold)) { sh = sh || spread_hosts(p); sp = sp || spread_pieces(p); }
        CHECK(sh);   // one Theft onto each Speaker
        CHECK(sp);   // one Theft moving each Hammer
    }
}

TEST_CASE("Equip host fold / Theft: only mirror images are dropped, every line survives -- Speakers AND Hammers")
{
    const GameState s = TheftBoard();
    const std::vector<TurnSolver::Plan> off = Menu(s, 0), on = Menu(s, 1);
    std::set<std::string> on_exact, on_orbit, off_exact;
    for (const TurnSolver::Plan& p : on)  { on_exact.insert(SigR(p, {})); on_orbit.insert(OrbitR(p)); }
    for (const TurnSolver::Plan& p : off) { off_exact.insert(SigR(p, {})); }
    std::set<std::string> off_orbit;
    for (const TurnSolver::Plan& p : off) { off_orbit.insert(OrbitR(p)); }
    MESSAGE("two-Speaker / two-Hammer / two-Theft board (autonomous menu): " << off_exact.size()
            << " distinct plans unfolded -> " << on_exact.size() << " folded; " << off_orbit.size()
            << " distinct lines up to relabelling");
    CHECK(on_exact.size() < off_exact.size());
    for (const std::string& k : on_exact) { CHECK_MESSAGE(off_exact.count(k) == 1, k); }
    for (const TurnSolver::Plan& p : off) { CHECK_MESSAGE(on_orbit.count(OrbitR(p)) == 1, SigR(p, {})); }
    // A MIXED plan -- the Lizard reconfigured onto one Speaker and a Theft onto the other -- survives.
    bool mixed = false;
    for (const TurnSolver::Plan& p : on)
    {
        bool reconf = false, theft = false;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::Equip && a.sac_source_id == 19 && a.sac_victim_id == kS1) { reconf = true; }
            if (IsTheftHf(a) && a.enchant_target == kS2) { theft = true; }
        }
        mixed = mixed || (reconf && theft);
    }
    CHECK(mixed);
}

TEST_CASE("Equip host fold / Theft: a dropped plan and its twin realise the same board up to the relabelling, in BOTH worlds")
{
    const GameState s = TheftBoard();
    const std::vector<TurnSolver::Plan> off = Menu(s, 0), on = Menu(s, 1);
    std::set<std::string> on_exact;
    for (const TurnSolver::Plan& p : on) { on_exact.insert(SigR(p, {})); }
    int checked = 0, thefts = 0;
    for (const TurnSolver::Plan& d : off)
    {
        if (on_exact.count(SigR(d, {}))) { continue; }
        const TurnSolver::Plan* twin = nullptr;
        Relabel used{};
        for (Relabel r : { Relabel{ true, false }, Relabel{ false, true }, Relabel{ true, true } })
        {
            for (const TurnSolver::Plan& p : on) { if (SigR(p, {}) == SigR(d, r)) { twin = &p; used = r; break; } }
            if (twin != nullptr) { break; }
        }
        REQUIRE_MESSAGE(twin != nullptr, "a dropped plan has no relabelled twin: ", SigR(d, {}));
        for (int world = 0; world < 2; ++world)
        {
            INFO((world == 0 ? "rollout" : "executor") << " " << SigR(d, {}));
            const GameState a = world == 0 ? RollHf(s, d) : ExecHf(s, d);
            const GameState b = world == 0 ? RollHf(s, *twin) : ExecHf(s, *twin);
            CHECK(BoardKeyR(a, used) == BoardKeyR(b, {}));
        }
        for (const Action& a : d.actions) { if (IsTheftHf(a)) { ++thefts; break; } }
        ++checked;
        if (checked >= 60) { break; }
    }
    CHECK(checked > 0);
    CHECK(thefts > 0);
    // ...and every folded-menu plan holding a Theft realises identically in both worlds.
    int lock = 0;
    for (const TurnSolver::Plan& p : on)
    {
        bool theft = false;
        for (const Action& a : p.actions) { if (IsTheftHf(a)) { theft = true; } }
        if (!theft) { continue; }
        INFO(SigR(p, {}));
        CHECK(BoardKeyR(RollHf(s, p), {}) == BoardKeyR(ExecHf(s, p), {}));
        if (++lock >= 80) { break; }
    }
    CHECK(lock > 0);
}

// ---- HAND pieces: one Theft variant per hand COPY (2026-10-17) -------------------------------------
// A same-plan hand Equipment used to be offered once per NAME, pinned to the first hand copy, so two
// Thefts of a "cast two Colossus Hammers, Theft each onto a different creature" line both named that
// copy: the second Theft moved the same Hammer again, and one Hammer on each host was unfindable at any
// budget. Now each hand copy is its own pin; the copies materialise in hand order (the k-th cast of a
// name takes the first copy still in hand, both worlds), so a pin onto the i-th copy rides a subset
// that casts at least i+1 of them and resolves after the (i+1)-th cast.
namespace
{
constexpr int kC1 = 61, kC2 = 63;   // two identical Colossus Hammers in hand
int MapC(int n, bool swap) { return !swap ? n : n == kC1 ? kC2 : n == kC2 ? kC1 : n; }
std::string SigC(const TurnSolver::Plan& p, bool swap)
{
    std::vector<std::string> v;
    for (const Action& a : p.actions)
    {
        v.push_back(std::to_string(static_cast<int>(a.kind)) + ":" + a.card_name.str() + ":"
                    + std::to_string(MapC(a.sac_source_id, swap)) + ">" + std::to_string(MapC(a.sac_victim_id, swap))
                    + "/e" + std::to_string(a.enchant_target) + "/m" + std::to_string(MapC(a.attach_equip_id, swap)));
    }
    std::sort(v.begin(), v.end());
    std::string k = static_cast<const std::string&>(p.land_to_play) + "|";
    for (const std::string& x : v) { k += x + ";"; }
    return k;
}
std::string BoardKeyC(const GameState& g, bool swap)
{
    std::vector<std::string> v;
    for (const Permanent& p : g.battlefield)
    {
        v.push_back(std::to_string(MapC(p.card.m_number, swap)) + "@" + std::to_string(MapC(p.equipped_to, swap))
                    + (p.tapped ? "T" : "u"));
    }
    std::sort(v.begin(), v.end());
    std::string k;
    for (const std::string& x : v) { k += x + ";"; }
    k += "|hand" + std::to_string(g.players[0].hand.size()) + "|gy" + std::to_string(g.players[0].graveyard.size());
    return k;
}
// Kor Duelist + Swiftblade Vindicator (two DIFFERENT hosts), two identical Hammers and two Thefts in
// hand, five Mountains: enough for cast + cast + Theft + Theft, not for an {8} equip.
GameState HandBoard()
{
    EnsureCardsHf();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < 20; ++k) { Card c = DefHf("Plains").card; c.m_number = 900 + k; s.players[0].library.push_back(c); }
    auto put = [&](const std::string& name, int number)
    {
        Permanent p; p.card = DefHf(name).card; p.card.m_number = number; p.controller_index = 0; p.owner_index = 0;
        s.battlefield.push_back(p);
    };
    put("Kor Duelist", 15);
    put("Swiftblade Vindicator", 57);
    for (int k = 0; k < 5; ++k) { put("Mountain", 70 + k); }
    for (int n : { kC1, kC2 }) { Card c = DefHf("Colossus Hammer").card; c.m_number = n; s.players[0].hand.push_back(c); }
    for (int n : { 60, 62 })   { Card c = DefHf("Magnetic Theft").card;  c.m_number = n; s.players[0].hand.push_back(c); }
    return s;
}
// Does the realised board hold BOTH hand Hammers, each on a different creature?
bool OneHammerOnEach(const GameState& g)
{
    int h1 = -1, h2 = -1;
    for (const Permanent& p : g.battlefield)
    {
        if (p.card.m_number == kC1) { h1 = p.equipped_to; }
        if (p.card.m_number == kC2) { h2 = p.equipped_to; }
    }
    return h1 > 0 && h2 > 0 && h1 != h2;
}
}   // namespace

TEST_CASE("Hand-piece Thefts: two freshly cast identical Hammers, each Thefted onto a different creature, is findable")
{
    const GameState s = HandBoard();
    for (int fold = 0; fold < 2; ++fold)
    {
        INFO("fold " << fold);
        bool found = false;
        for (const TurnSolver::Plan& p : Menu(s, fold))
        {
            int thefts = 0;
            for (const Action& a : p.actions) { if (IsTheftHf(a)) { ++thefts; } }
            if (thefts != 2) { continue; }
            if (OneHammerOnEach(RollHf(s, p))) { found = true; break; }
        }
        CHECK(found);
    }
}

TEST_CASE("Hand-piece Thefts: only mirror images of the two copies are dropped; twins realise the same board in BOTH worlds")
{
    const GameState s = HandBoard();
    const std::vector<TurnSolver::Plan> off = Menu(s, 0), on = Menu(s, 1);
    std::set<std::string> on_exact, on_orbit, off_exact;
    for (const TurnSolver::Plan& p : on)  { on_exact.insert(SigC(p, false)); on_orbit.insert(std::min(SigC(p, false), SigC(p, true))); }
    for (const TurnSolver::Plan& p : off) { off_exact.insert(SigC(p, false)); }
    MESSAGE("two hand Hammers + two Thefts (autonomous menu): " << off_exact.size() << " distinct plans unfolded -> "
            << on_exact.size() << " folded");
    for (const std::string& k : on_exact) { CHECK_MESSAGE(off_exact.count(k) == 1, k); }
    for (const TurnSolver::Plan& p : off)
    { CHECK_MESSAGE(on_orbit.count(std::min(SigC(p, false), SigC(p, true))) == 1, SigC(p, false)); }
    int dropped = 0;
    for (const TurnSolver::Plan& d : off)
    {
        if (on_exact.count(SigC(d, false))) { continue; }
        const TurnSolver::Plan* twin = nullptr;
        for (const TurnSolver::Plan& p : on) { if (SigC(p, false) == SigC(d, true)) { twin = &p; break; } }
        REQUIRE_MESSAGE(twin != nullptr, "a dropped plan has no twin: ", SigC(d, false));
        for (int world = 0; world < 2; ++world)
        {
            INFO((world == 0 ? "rollout" : "executor") << " " << SigC(d, false));
            const GameState a = world == 0 ? RollHf(s, d) : ExecHf(s, d);
            const GameState b = world == 0 ? RollHf(s, *twin) : ExecHf(s, *twin);
            CHECK(BoardKeyC(a, true) == BoardKeyC(b, false));
        }
        ++dropped;
    }
    CHECK(dropped > 0);
    // Every folded-menu plan with a Theft realises identically in both worlds, and a Theft pinned to a
    // hand copy really lands on THAT copy.
    int lock = 0;
    for (const TurnSolver::Plan& p : on)
    {
        bool theft = false;
        for (const Action& a : p.actions) { if (IsTheftHf(a)) { theft = true; } }
        if (!theft) { continue; }
        INFO(SigC(p, false));
        const GameState r = RollHf(s, p);
        CHECK(BoardKeyC(r, false) == BoardKeyC(ExecHf(s, p), false));
        for (const Action& a : p.actions)
        {
            if (!IsTheftHf(a) || a.attach_equip_hand.empty()) { continue; }
            const Permanent* piece = nullptr;
            for (const Permanent& q : r.battlefield) { if (q.card.m_number == a.attach_equip_id) { piece = &q; } }
            CHECK(piece != nullptr);
        }
        ++lock;
    }
    CHECK(lock > 0);
}
