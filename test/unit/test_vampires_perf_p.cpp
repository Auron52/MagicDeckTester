// Unit tests for the Vampires search-cost round 2, lane P (per-node CPU, BYTE-IDENTICAL speedups;
// docs/design/vampires-search-cost-2.md):
//   * MTG_WALK_DFS -- the plain scalar subset walk enumerated depth-first (PlainOdometerDfs) emits exactly
//     the selections the carry walk emits, in the same order;
//   * MTG_VAMP_RULE_PRE -- SubsetVampOutletInvalid on the per-enumeration cached facts (VampRulePre)
//     answers every selection exactly as the per-subset rules do, on real CollectActions candidate lists
//     of rich Vampires boards.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "deck/DeckLoader.h"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace
{

void EnsureCardsLoadedP()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(
            ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefP(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

GameState FreshP(Phase phase)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.phase               = phase;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife() - 4;
    s.opponent_lost_life_this_turn = true;   // Falkenrath Pit Fighter's activation condition
    s.m_provider = &SelectDecisionProvider(DeckLoader::LoadFromFile(
        ResolveHeuristicDefaultsPath("decks/Vampires/Vampire.cod")));
    s.deck_has_madness = true;
    for (int k = 0; k < 20; ++k)
    {
        Card c = DefP("Swamp").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

void PutP(GameState& s, const std::string& name, int number, bool sick = false)
{
    Permanent p;
    p.card              = DefP(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
}

void HandP(GameState& s, const std::string& name, int number)
{
    Card c = DefP(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

// Random selections over `cands` (distinct indices, ascending like an odometer's), both fold modes;
// every one must agree. Returns how many were rejected (so a test can insist the sample is not vacuous).
int CheckRulePreAgrees(const GameState& s, const std::vector<Action>& cands, int samples, std::uint32_t seed)
{
    std::mt19937 rng(seed);
    int rejected = 0;
    const int n = static_cast<int>(cands.size());
    REQUIRE(n > 0);
    for (int t = 0; t < samples; ++t)
    {
        std::vector<int> sel;
        const int want = 1 + static_cast<int>(rng() % 7u);
        for (int j = 0; j < n; ++j)
        { if (static_cast<int>(rng() % static_cast<std::uint32_t>(n)) < want) { sel.push_back(j); } }
        if (sel.empty()) { sel.push_back(static_cast<int>(rng() % static_cast<std::uint32_t>(n))); }
        for (int fold = 0; fold < 2; ++fold)
        {
            bool slow = false, fast = false;
            TurnSolver::VampRulePreAgreeForTest(s, cands, sel, fold != 0, slow, fast);
            REQUIRE_MESSAGE(slow == fast, "VampRulePre disagrees on a selection of ", sel.size(), " actions");
            if (slow) { ++rejected; }
        }
    }
    return rejected;
}

}   // namespace

TEST_CASE("MTG_WALK_DFS: the depth-first plain walk emits the carry walk's selections, in order")
{
    std::mt19937 rng(20261021u);
    int nonempty = 0;
    for (int trial = 0; trial < 400; ++trial)
    {
        const int G = static_cast<int>(rng() % 6u);
        const int I = static_cast<int>(rng() % 4u);
        std::vector<std::vector<int>> groups(static_cast<std::size_t>(G));
        std::vector<int> independent, auto_sel, cand_mv;
        int next = 0;
        auto add = [&]() { cand_mv.push_back(static_cast<int>(rng() % 5u)); return next++; };
        if (rng() % 3u == 0) { auto_sel.push_back(add()); }
        for (int g = 0; g < G; ++g)
        {
            const int sz = 1 + static_cast<int>(rng() % 4u);
            for (int k = 0; k < sz; ++k) { groups[static_cast<std::size_t>(g)].push_back(add()); }
        }
        for (int b = 0; b < I; ++b) { independent.push_back(add()); }
        const int bound = (rng() % 5u == 0) ? 1 << 30 : static_cast<int>(rng() % 9u);
        const auto ref = TurnSolver::PlainWalkSequenceForTest(groups, independent, auto_sel, cand_mv, bound, false);
        const auto dfs = TurnSolver::PlainWalkSequenceForTest(groups, independent, auto_sel, cand_mv, bound, true);
        REQUIRE(ref == dfs);
        if (!ref.empty()) { ++nonempty; }
    }
    CHECK(nonempty > 200);
}

TEST_CASE("MTG_VAMP_RULE_PRE: the cached rule facts agree with the per-subset rules (main 1, rich board)")
{
    EnsureCardsLoadedP();
    GameState s = FreshP(Phase::PreCombatMain);
    PutP(s, "Stromkirk Condemned", 1);
    PutP(s, "Stromkirk Condemned", 2);
    PutP(s, "Falkenrath Pit Fighter", 3);
    PutP(s, "Legion Lieutenant", 4);
    PutP(s, "Vampire of the Dire Moon", 5, /*sick=*/true);
    PutP(s, "Bloodline Keeper", 6);
    PutP(s, "Captivating Vampire", 8);
    PutP(s, "Voldaren Estate", 58);
    PutP(s, "Voldaren Estate", 59);
    PutP(s, "Blood Token", 70);
    PutP(s, "Swamp", 50); PutP(s, "Swamp", 51); PutP(s, "Blackcleave Cliffs", 52); PutP(s, "Unclaimed Territory", 53);
    {
        Permanent opp;
        opp.card = DefP("Vampire of the Dire Moon").card;
        opp.card.m_number = 300;
        opp.controller_index = 1;
        opp.owner_index = 1;
        s.battlefield.push_back(opp);
    }
    HandP(s, "Markov Baron", 10);
    HandP(s, "Falkenrath Gorger", 11);
    HandP(s, "Bloodghast", 12);
    HandP(s, "Legion Lieutenant", 13);
    HandP(s, "Legion Lieutenant", 14);
    HandP(s, "Swamp", 15);
    HandP(s, "Asylum Visitor", 16);
    {
        Card g = DefP("Bloodghast").card;
        g.m_number = 400;
        s.players[0].graveyard.push_back(g);
    }
    const std::vector<Action> cands = TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/true);
    REQUIRE(cands.size() > 10);
    const int rej = CheckRulePreAgrees(s, cands, 6000, 7u);
    CHECK(rej > 100);
    CHECK(rej < 12000);
}

TEST_CASE("MTG_VAMP_RULE_PRE: agreement in main 2 and with a thin hand (rule 8's hand count binds)")
{
    EnsureCardsLoadedP();
    GameState s = FreshP(Phase::PostCombatMain);
    PutP(s, "Stromkirk Condemned", 1);
    PutP(s, "Falkenrath Pit Fighter", 3);
    PutP(s, "Falkenrath Pit Fighter", 7);
    PutP(s, "Legion Lieutenant", 4);
    PutP(s, "Voldaren Estate", 58);
    PutP(s, "Blood Token", 70);
    PutP(s, "Blood Token", 71);
    PutP(s, "Swamp", 50); PutP(s, "Swamp", 51); PutP(s, "Swamp", 52);
    HandP(s, "Markov Baron", 10);
    HandP(s, "Falkenrath Gorger", 11);
    const std::vector<Action> cands = TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/false);
    REQUIRE(cands.size() > 3);
    const int rej = CheckRulePreAgrees(s, cands, 6000, 11u);
    CHECK(rej > 50);
}
