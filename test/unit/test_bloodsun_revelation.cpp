// Blood Sun step 6 (docs/design/blood-sun-pipeline.md §4, §12 step 6): Diabolic Revelation's package axis -- the
// rank-lex generator yields every multiset exactly once (against the closed form), fold F-R2 (Lotus Vale == Lotus
// Field) under a Blood Sun nothing can end ONLY, the one-shuffle resolution in both worlds, the wave-0 emission, and
// the generator-stop: no wave package is generated at a node whose best line already wins this round.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
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
void EnsureCards6()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}
const CardDefinition& Def6(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}
Card Card6(const std::string& name, int number)
{
    Card c = Def6(name).card;
    c.m_number = number;
    return c;
}
void Put6(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card = Card6(name, number);
    p.controller_index = 0;
    p.owner_index = 0;
    s.battlefield.push_back(p);
}
GameState Base6()
{
    static const BloodSunProvider bp;
    EnsureCards6();
    GameState s;
    s.m_provider          = &bp;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.deck_has_landfall   = false;
    s.deck_has_revolt     = false;
    s.deck_has_land_ability_loss = true;
    s.own_death_live      = true;
    return s;
}
// These tests pin the GENERATOR's mechanics on libraries of non-deck cards (Lightning Bolt, basics), so they run with
// the step-6c USER target restriction OFF (MTG_BLOODSUN_USER_PRUNE; test_bloodsun_userprune.cpp tests it ON).
struct Unrestricted6
{
    Unrestricted6()  { heurarm::t_arm[heurarm::BLOODSUN_USER_PRUNE] = 0; }
    ~Unrestricted6() { heurarm::t_arm[heurarm::BLOODSUN_USER_PRUNE] = -1; }
};
int Count6(const std::vector<Card>& zone, const std::string& name)
{
    int n = 0;
    for (const Card& c : zone) { if (c.m_name.str() == name) { ++n; } }
    return n;
}
}   // namespace

TEST_CASE("revelation: params, and the rank-lex generator yields every multiset exactly once (closed form)")
{
    const Unrestricted6 unrestricted;
    EnsureCards6();
    const CardDefinition& dr = Def6("Diabolic Revelation");
    CHECK(dr.params.tutor_up_to_x);
    CHECK(dr.params.tutor_to_hand);
    CHECK(dr.params.tutor_shuffle_after);
    CHECK(dr.card.m_mana_cost.has_x);
    for (const std::vector<int>& caps : { std::vector<int>{ 3, 2, 1 }, std::vector<int>{ 4, 4, 1, 2, 3 },
                                          std::vector<int>{ 1, 1, 1, 1, 1, 1, 1 }, std::vector<int>{ 7 } })
    {
        int total = 0;
        for (int c : caps) { total += c; }
        for (int X = 1; X <= total + 1; ++X)
        {
            std::set<std::vector<int>> seen;
            std::vector<int> c;
            long long n = 0;
            if (revpkg::FirstFill(c, caps, X))
            {
                std::vector<int> prev;
                do
                {
                    int sum = 0;
                    for (std::size_t i = 0; i < c.size(); ++i) { CHECK(c[i] >= 0); CHECK(c[i] <= caps[i]); sum += c[i]; }
                    CHECK(sum == X);
                    if (!prev.empty()) { CHECK(c < prev); }   // strictly lex-descending: a total order, no repeats
                    prev = c;
                    CHECK(seen.insert(c).second);
                    ++n;
                } while (revpkg::NextLex(c, caps));
            }
            CHECK(n == revpkg::CountMultisets(caps, X));
        }
    }
    CHECK(revpkg::CountMultisets({ 3, 2, 1 }, 3) == 6);   // 300 210 201 120 111 021 -> 6 (030 impossible)
    CHECK(revpkg::WaveOf(0) == 0);
    CHECK(revpkg::WaveOf(revpkg::kWave0PerX - 1) == 0);
    CHECK(revpkg::WaveOf(revpkg::kWave0PerX) == 1);
    CHECK(revpkg::WaveOf(revpkg::WaveEnd(1)) == 2);
    const std::string enc = revpkg::EncodePackage(2, { "Emrakul, the Aeons Torn", "Scorched Ruins" });
    CHECK(revpkg::PackageWave(enc) == 2);
    CHECK(revpkg::DecodePackage(enc) == std::vector<std::string>{ "Emrakul, the Aeons Torn", "Scorched Ruins" });
}

TEST_CASE("revelation F-R2: Lotus Vale == Lotus Field under a Blood Sun nothing can end, and only then")
{
    const Unrestricted6 unrestricted;
    auto lotus_classes = [](const GameState& s) -> int
    {
        int n = 0;
        for (const revpkg::Class& k : revpkg::BuildClasses(s, 0))
        { for (const auto& m : k.members) { if (m.first == "Lotus Vale" || m.first == "Lotus Field") { ++n; break; } } }
        return n;
    };
    GameState s = Base6();
    s.players[0].library.push_back(Card6("Lotus Vale", 1));
    s.players[0].library.push_back(Card6("Lotus Vale", 2));
    s.players[0].library.push_back(Card6("Lotus Field", 3));
    s.players[0].library.push_back(Card6("Swamp", 4));
    CHECK(lotus_classes(s) == 2);                       // no Blood Sun: two classes
    Put6(s, "Blood Sun", 50);
    CHECK(lotus_classes(s) == 1);                       // Blood Sun: one twin class...
    for (const revpkg::Class& k : revpkg::BuildClasses(s, 0))
    { if (k.members.front().first == "Lotus Field") { CHECK(k.cap == 3); } }
    GameState u = s;
    u.players[0].library.push_back(Card6("Ulamog, the Ceaseless Hunger", 5));
    CHECK(lotus_classes(u) == 2);                       // ...unless a card that can exile it is reachable
    GameState w = s;
    w.players[0].library.push_back(Card6("Worldfire", 6));
    CHECK(lotus_classes(w) == 1);                       // Worldfire exiles the hand too: the twin holds
    GameState n = s;
    n.deck_has_land_ability_loss = false;               // an unstamped-off deck: never suppressed
    CHECK(lotus_classes(n) == 2);
}

TEST_CASE("revelation: resolution moves the package to hand and shuffles ONCE, in both worlds")
{
    const Unrestricted6 unrestricted;
    GameState s = Base6();
    for (int k = 0; k < 5; ++k) { s.players[0].library.push_back(Card6("Swamp", 100 + k)); }
    s.players[0].library.push_back(Card6("Lightning Bolt", 200));
    s.players[0].library.push_back(Card6("Lightning Bolt", 201));
    const std::uint64_t sc0 = s.search_count;
    StackEntry e;
    e.type             = StackEntry::EntryType::Spell;
    e.source           = Card6("Diabolic Revelation", 300);
    e.controller_index = 0;
    e.chosen_x         = 3;
    e.tutor_target     = revpkg::EncodePackage(0, { "Lightning Bolt", "Lightning Bolt", "Swamp" });
    EffectHandler::Resolve(s, e, Def6("Diabolic Revelation"));
    CHECK(Count6(s.players[0].hand, "Lightning Bolt") == 2);
    CHECK(Count6(s.players[0].hand, "Swamp") == 1);
    CHECK(s.players[0].library.size() == 4);
    CHECK(s.search_count == sc0 + 1);
    // X caps the package; an absent name is skipped ("up to").
    GameState t = Base6();
    for (int k = 0; k < 3; ++k) { t.players[0].library.push_back(Card6("Swamp", 400 + k)); }
    PerformTutorPackageToHand(t, 0, Def6("Diabolic Revelation"), 1,
                              revpkg::EncodePackage(0, { "Lightning Bolt", "Swamp", "Swamp" }));
    CHECK(t.players[0].hand.empty());                   // the first name (Bolt) is absent; X = 1 caps the rest
    CHECK(t.players[0].library.size() == 3);
}

TEST_CASE("revelation: wave 0 emits the first K0 packages of every X; X = |S| >= 1; every cast carries a package")
{
    const Unrestricted6 unrestricted;
    GameState s = Base6();
    for (int k = 0; k < 9; ++k) { Put6(s, "Swamp", 10 + k); }   // 9 mana: X <= 4
    s.players[0].hand.push_back(Card6("Diabolic Revelation", 1));
    const std::vector<std::string> lib = { "Forest", "Island", "Lightning Bolt", "Exploration", "Fastbond", "Mountain" };
    for (std::size_t k = 0; k < lib.size(); ++k) { s.players[0].library.push_back(Card6(lib[k], 100 + static_cast<int>(k))); }
    std::vector<int> per_x(10, 0);
    std::set<std::string> encs;
    for (const Action& a : TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/true))
    {
        if (a.card_name.str() != "Diabolic Revelation") { continue; }
        REQUIRE(revpkg::IsPackage(a.tutor_target.str()));
        CHECK(revpkg::PackageWave(a.tutor_target.str()) == 0);
        CHECK(static_cast<int>(revpkg::DecodePackage(a.tutor_target.str()).size()) == a.chosen_x);
        CHECK(a.chosen_x >= 1);
        CHECK(encs.insert(a.tutor_target.str()).second);
        ++per_x[static_cast<std::size_t>(a.chosen_x)];
    }
    CHECK(per_x[0] == 0);
    for (int X = 1; X <= 4; ++X)
    {
        const long long all = revpkg::CountMultisets({ 1, 1, 1, 1, 1, 1 }, X);
        CHECK(per_x[static_cast<std::size_t>(X)] == std::min<long long>(all, revpkg::kWave0PerX));
    }
    CHECK(per_x[5] == 0);   // 9 mana pays X = 4 at most
}

TEST_CASE("revelation generator-stop: a wave-0 win this round generates no wave package; a wave-1 win is found")
{
    const Unrestricted6 unrestricted;
    auto run = [](const std::vector<std::string>& lib, bool& won_now) -> long long
    {
        GameState s = Base6();
        s.players[1].life = 3;
        for (int k = 0; k < 6; ++k) { Put6(s, "Swamp", 10 + k); }
        Put6(s, "Mountain", 20);                         // 7 mana: X = 1 + Lightning Bolt is the only kill
        s.players[0].hand.push_back(Card6("Diabolic Revelation", 1));
        for (std::size_t k = 0; k < lib.size(); ++k)
        { s.players[0].library.push_back(Card6(lib[k], 100 + static_cast<int>(k))); }
        TurnSolver::ResetRevelationWaveCountersForTest();
        const TurnSolver::SearchLine line = TurnSolver::FullSearchLine(s, 1, 8, /*second_main=*/false, nullptr, nullptr);
        won_now = (line.win_turn == s.turn_number);
        return TurnSolver::RevelationWavePackagesForTest();
    };
    bool won = false;
    // Bolt is ordinal 3 of X = 1 (wave 0: the three lands rank first, Worldfire last) and Worldfire's ordinal 4 holds
    // wave 1 back: the node wins this round, so the pending wave is never generated.
    CHECK(run({ "Forest", "Lightning Bolt", "Island", "Worldfire", "Plains" }, won) == 0);
    CHECK(won);
    // Bolt is ordinal 4 of X = 1 (wave 1: four lands rank first): only the wave phase can find it -- and does.
    won = false;
    CHECK(run({ "Forest", "Island", "Plains", "Mountain", "Lightning Bolt" }, won) > 0);
    CHECK(won);
}

// ---- Step 6 part 2: every host reaches every package (completeness at an unlimited budget) --------------------------
namespace
{
// 7 mana (six Swamps + a Mountain), opponent at 3: Revelation X = 1 for Lightning Bolt, then Bolt, is the only kill.
// Bolt is ordinal 4 of X = 1 (the four lands rank first), so its package is WAVE 1: no host finds it from wave 0.
GameState WaveOneBolt6()
{
    GameState s = Base6();
    s.players[1].life = 3;
    for (int k = 0; k < 6; ++k) { Put6(s, "Swamp", 10 + k); }
    Put6(s, "Mountain", 20);
    s.players[0].hand.push_back(Card6("Diabolic Revelation", 1));
    const std::vector<std::string> lib = { "Forest", "Island", "Plains", "Mountain", "Lightning Bolt" };
    for (std::size_t k = 0; k < lib.size(); ++k) { s.players[0].library.push_back(Card6(lib[k], 100 + static_cast<int>(k))); }
    return s;
}
// A Revelation cast whose package holds the Bolt ({Bolt} at X = 1, or {Bolt, Mountain} at X = 2: the Mountain is the
// land drop that pays for the Bolt), at wave >= min_wave.
bool IsBoltPackage6(const Action& a, int min_wave)
{
    if (a.card_name.str() != "Diabolic Revelation" || !revpkg::IsPackage(a.tutor_target.str())) { return false; }
    std::vector<std::string> names = revpkg::DecodePackage(a.tutor_target.str());
    std::sort(names.begin(), names.end());
    const bool kill = names == std::vector<std::string>{ "Lightning Bolt" }
                   || names == std::vector<std::string>{ "Lightning Bolt", "Mountain" };
    return revpkg::PackageWave(a.tutor_target.str()) >= min_wave && kill;
}
}   // namespace

TEST_CASE("revelation waves, executor fallback host (SolveWithLookahead): a wave-1-only package is reached")
{
    const Unrestricted6 unrestricted;
    const GameState s = WaveOneBolt6();
    TurnSolver::ResetRevelationWaveCountersForTest();
    int win = -1;
    const TurnSolver::Plan p = TurnSolver::SolveWithLookahead(s, /*is_pre_combat=*/true, /*depth=*/1, /*max_turns=*/8,
                                                              nullptr, /*enforce_budget=*/true, false, nullptr, &win);
    bool bolt = false;
    for (const Action& a : p.actions) { bolt = bolt || IsBoltPackage6(a, 1); }
    CHECK(bolt);
    CHECK(win == s.turn_number);
    CHECK(TurnSolver::RevelationWavePackagesForTest() > 0);
}

TEST_CASE("revelation waves, exhaustive menu host (EnumerateMainPlansExhaustive): the wave-1 package is on the menu")
{
    const Unrestricted6 unrestricted;
    const GameState s = WaveOneBolt6();
    bool truncated = true;
    int n_bolt = 0;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlansExhaustive(s, true, nullptr, &truncated))
    { for (const Action& a : p.actions) { if (IsBoltPackage6(a, 1)) { ++n_bolt; } } }
    CHECK_FALSE(truncated);
    CHECK(n_bolt >= 1);
    // Wave 0 alone does not hold it (the package really is beyond wave 0).
    int n_bolt0 = 0;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    { for (const Action& a : p.actions) { if (IsBoltPackage6(a, 0)) { ++n_bolt0; } } }
    CHECK(n_bolt0 == 0);
}

TEST_CASE("revelation waves, breakpoint continuation host: the wave-k continuation list holds the wave-k packages")
{
    const Unrestricted6 unrestricted;
    // A continuation list (the one a breakpoint's continuation, in the rollout AND the executor, indexes) admits wave 0;
    // the wave-k list (Plan::rv_cont_wave) holds exactly wave k's tranche. The kill package is wave 1 here.
    const GameState s = WaveOneBolt6();
    int kill0 = 0, kill1 = 0, other_wave1 = 0;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateBreakpointPlans(s, true))
    { for (const Action& a : p.actions) { if (IsBoltPackage6(a, 0)) { ++kill0; } } }
    const std::vector<TurnSolver::Plan> w1 = TurnSolver::EnumerateBreakpointPlansAtWave(s, true, 1);
    for (const TurnSolver::Plan& p : w1)
    {
        int maxw = -1;
        for (const Action& a : p.actions)
        {
            if (IsBoltPackage6(a, 1)) { ++kill1; }
            if (revpkg::IsPackage(a.tutor_target.str())) { maxw = std::max(maxw, revpkg::PackageWave(a.tutor_target.str())); }
        }
        if (maxw != 1) { ++other_wave1; }
    }
    CHECK(kill0 == 0);
    CHECK(kill1 >= 1);
    CHECK(other_wave1 == 0);   // the tranche filter: every wave-1 continuation carries a wave-1 package
    // A different wave is a different (cached) list, and asking again serves the same one.
    CHECK(TurnSolver::EnumerateBreakpointPlansAtWave(s, true, 1).size() == w1.size());
}
