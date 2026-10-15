// Unit tests for closed land-search gaps (docs/design/land-search-cluster.md, 2026-10-15):
//
//  GAP 2 -- SEARCH FOR TOMORROW HARD-CAST MANA. Its basic enters UNTAPPED, so a later cast of the same
//  main phase may spend it. The cast now carries a LAND-PUT CREDIT on Action::rock_mana, emitted once
//  per credit class of its legal picks, and its resolution is RESTRICTED to the picks earning that
//  credit (core/LandSearch.h LandSearchCreditGroups / LandSearchCreditFilter) -- in the rollout through
//  apply_one's cast_land_put, in the executor through StackEntry::land_put_credit. Pinned:
//   * the credit is the pick's own colour, split per class, front class first;
//   * "Search for Tomorrow, then Rampant Growth off the fetched Forest" is ENUMERATED (4 mana of board
//     for 5 of cost), and the variant credits {G} even though the provider's FRONT pick is a Mountain;
//   * the funding order casts Search for Tomorrow first, and the rollout and the executor realise the
//     same line (Forest fetched UNTAPPED, Rampant Growth paid with it);
//   * no basic left -> no credit, and the line is not offered.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{

void LoadCardsGaps()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DG(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

int PutG(GameState& s, const std::string& name, int number, bool tapped, bool sick = false)
{
    Permanent p;
    p.card              = DG(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.tapped            = tapped;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

void LibG(GameState& s, const std::string& name, int number)
{
    Card c = DG(name).card;
    c.m_number = number;
    s.players[0].library.push_back(c);
}

void HandG(GameState& s, const std::string& name, int number)
{
    Card c = DG(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

GameState FreshG(int turn = 4)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.game_seed           = 4242;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    return s;
}

int CountBf(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

bool InHandG(const GameState& s, const std::string& name)
{
    for (const Card& c : s.players[0].hand) { if (c.m_name.str() == name) { return true; } }
    return false;
}

// The plan's CastFromHand action of `name`, or nullptr.
const Action* CastOf(const TurnSolver::Plan& p, const std::string& name)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { return &a; } }
    return nullptr;
}

// ---- GAP 2 board ---------------------------------------------------------------------------------
// Turn 4: Forest + two Radiant Fountains + Haunted Fengraf untapped ({G} + three {C}); hand Search for
// Tomorrow {2}{G}, Rampant Growth {1}{G} and Phylath {4}{R}{G}. Five mana of cost on four lands, and
// the SECOND {G} can only be the basic Search for Tomorrow fetches. Phylath's {R} with no red source
// makes the provider's FRONT pick a Mountain -- so the {G} line exists only as the Forest variant.
GameState SftBoard(bool with_basics)
{
    GameState s = FreshG();
    PutG(s, "Forest", 1, false);
    PutG(s, "Radiant Fountain", 2, false);
    PutG(s, "Radiant Fountain", 3, false);
    PutG(s, "Haunted Fengraf", 4, false);
    HandG(s, "Search for Tomorrow", 20);
    HandG(s, "Rampant Growth", 21);
    HandG(s, "Phylath, World Sculptor", 22);
    if (with_basics)
    {
        LibG(s, "Mountain", 50); LibG(s, "Mountain", 51);
        LibG(s, "Forest", 52);   LibG(s, "Forest", 53);
    }
    else
    {
        LibG(s, "Gruul Turf", 50); LibG(s, "Radiant Fountain", 51);
    }
    for (int i = 0; i < 12; ++i) { LibG(s, "Avenger of Zendikar", 60 + i); }
    s.players[0].lands_played_this_turn = 1;   // the drop is spent: the casts are the whole decision
    return s;
}

const TurnSolver::Plan* FindSftAndRampant(const std::vector<TurnSolver::Plan>& plans, int want_green)
{
    for (const TurnSolver::Plan& p : plans)
    {
        const Action* sft = CastOf(p, "Search for Tomorrow");
        if (sft == nullptr || CastOf(p, "Rampant Growth") == nullptr) { continue; }
        if (want_green < 0 || sft->rock_mana.green == want_green) { return &p; }
    }
    return nullptr;
}

}   // namespace

TEST_CASE("Land-put credit: one credit per pick CLASS, front class first; basics only")
{
    LoadCardsGaps();
    const LandSearchSpec sft = LandSearchSpecOf(DG("Search for Tomorrow").params);
    CHECK(LandSearchPutsUntapped(DG("Search for Tomorrow")));
    CHECK_FALSE(LandSearchPutsUntapped(DG("Rampant Growth")));     // tapped: no same-turn mana
    CHECK_FALSE(LandSearchPutsUntapped(DG("Cultivate")));          // tapped (and one to hand)
    CHECK_FALSE(LandSearchPutsUntapped(DG("Primeval Titan")));     // a creature's ETB, tapped
    {
        GameState s = SftBoard(true);
        // Phylath's {R} with no red source: the provider's front pick is the Mountain.
        const std::vector<std::vector<std::string>> picks = LandSearchPicks(s, 0, sft);
        REQUIRE(picks.size() == 2);
        CHECK(picks.front() == std::vector<std::string>{ "Mountain" });
        const std::vector<ManaPool> g = LandSearchCreditGroups(s, 0, sft);
        REQUIRE(g.size() == 2);
        CHECK(g[0].red == 1);
        CHECK(g[0].Total() == 1);
        CHECK(g[1].green == 1);
        CHECK(g[1].Total() == 1);
    }
    {
        // No basic in the library: nothing creditable (fail toward NOT crediting).
        GameState s = SftBoard(false);
        CHECK(LandSearchCreditGroups(s, 0, sft).empty());
    }
}

TEST_CASE("Land-put credit: Search for Tomorrow then Rampant Growth off the fetched Forest is enumerated, hoisted, and realised identically by the rollout and the executor")
{
    LoadCardsGaps();
    const GameState s = SftBoard(true);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* both = FindSftAndRampant(plans, /*want_green=*/1);
    REQUIRE_MESSAGE(both != nullptr, "Search for Tomorrow + Rampant Growth (Forest credit) was not enumerated");
    // The {R} class cannot fund Rampant Growth's {G}: never offered with it (no phantom colour).
    for (const TurnSolver::Plan& p : plans)
    {
        const Action* sft = CastOf(p, "Search for Tomorrow");
        if (sft == nullptr || CastOf(p, "Rampant Growth") == nullptr) { continue; }
        CHECK(sft->rock_mana.green == 1);
        CHECK(sft->rock_mana.red == 0);
    }
    // Search for Tomorrow ALONE is still offered in both classes (the classes partition the picks).
    std::set<int> alone_reds;
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() != 1) { continue; }
        const Action* sft = CastOf(p, "Search for Tomorrow");
        if (sft != nullptr) { alone_reds.insert(sft->rock_mana.red); }
    }
    CHECK(alone_reds == std::set<int>{ 0, 1 });

    // The funding order both apply worlds run: Search for Tomorrow first, whatever order the plan holds.
    int sft_i = -1, rg_i = -1;
    for (int i = 0; i < static_cast<int>(both->actions.size()); ++i)
    {
        if (both->actions[static_cast<std::size_t>(i)].card_name.str() == "Search for Tomorrow") { sft_i = i; }
        if (both->actions[static_cast<std::size_t>(i)].card_name.str() == "Rampant Growth")      { rg_i  = i; }
    }
    REQUIRE(sft_i >= 0);
    REQUIRE(rg_i >= 0);
    std::vector<int> order{ rg_i, sft_i };
    ApplyPayableCastOrder(s, both->actions, order);
    CHECK(order == std::vector<int>{ sft_i, rg_i });

    // ROLLOUT: the plan applies in full -- the Forest (not the provider's front Mountain) arrives
    // untapped and pays Rampant Growth, whose own basic enters tapped.
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *both, /*is_pre_combat=*/true);
    CHECK_FALSE(InHandG(roll, "Search for Tomorrow"));
    CHECK_FALSE(InHandG(roll, "Rampant Growth"));
    CHECK(CountBf(roll, "Forest") >= 2);

    // EXECUTOR: the same two casts through the real cast path (pay, stack entry, EffectHandler), in
    // the funding order, the credit armed exactly as the plan-execution sites arm it.
    GameState ex = s;
    AIEngine eng;
    for (int idx : order)
    {
        const Action& a = both->actions[static_cast<std::size_t>(idx)];
        auto it = std::find_if(ex.players[0].hand.begin(), ex.players[0].hand.end(),
                               [&](const Card& c) { return c.m_name == a.card_name; });
        REQUIRE(it != ex.players[0].hand.end());
        MtgTestSeam::SetPendingLandPut(eng, a.rock_mana);
        ManaPool avail = AvailableManaPool(ex);
        const std::size_t before = ex.stack.size();
        MtgTestSeam::CastSpellFromHand(eng, ex, *it, avail, 0);
        REQUIRE_MESSAGE(ex.stack.size() == before + 1, "executor could not cast ", a.card_name.str());
        const StackEntry entry = ex.stack.back();
        ex.stack.pop_back();
        if (a.card_name.str() == "Search for Tomorrow")
        {
            CHECK(entry.land_put_credit.green == 1);
            CHECK(entry.land_put_credit.Total() == 1);
        }
        else { CHECK(entry.land_put_credit.Total() == 0); }
        EffectHandler::Resolve(ex, entry, DG(a.card_name.str()));
        if (a.card_name.str() == "Search for Tomorrow")
        {
            // The restricted pick: an UNTAPPED Forest, not the front-pick Mountain.
            int untapped_forests = 0;
            for (const Permanent& p : ex.battlefield)
            { if (p.card.m_name.str() == "Forest" && !p.tapped) { ++untapped_forests; } }
            CHECK(untapped_forests >= 1);
            CHECK(CountBf(ex, "Mountain") == 0);
        }
    }
    // Both worlds end on the same zones (names; which source tapped for what may differ).
    auto names = [](const std::vector<Card>& z)
    { std::multiset<std::string> m; for (const Card& c : z) { m.insert(c.m_name.str()); } return m; };
    auto bf_names = [](const GameState& g)
    { std::multiset<std::string> m; for (const Permanent& p : g.battlefield) { m.insert(p.card.m_name.str()); } return m; };
    CHECK(bf_names(roll) == bf_names(ex));
    CHECK(names(roll.players[0].hand) == names(ex.players[0].hand));
    CHECK(names(roll.players[0].graveyard) == names(ex.players[0].graveyard));
    CHECK(roll.players[0].library.size() == ex.players[0].library.size());
}

TEST_CASE("Land-put credit: no basic left -> no credit, and the line is not offered")
{
    LoadCardsGaps();
    const GameState s = SftBoard(false);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    CHECK(FindSftAndRampant(plans, -1) == nullptr);
    for (const TurnSolver::Plan& p : plans)
    {
        const Action* sft = CastOf(p, "Search for Tomorrow");
        if (sft != nullptr) { CHECK(sft->rock_mana.Total() == 0); }
    }
}

TEST_CASE("Land-put credit: a one-class library credits one action, and the resolution is unrestricted by it")
{
    LoadCardsGaps();
    GameState s = SftBoard(true);
    // Forests only.
    s.players[0].library = Library{};
    LibG(s, "Forest", 52); LibG(s, "Forest", 53);
    for (int i = 0; i < 8; ++i) { LibG(s, "Avenger of Zendikar", 60 + i); }
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    std::set<int> greens, reds;
    for (const TurnSolver::Plan& p : plans)
    {
        const Action* sft = CastOf(p, "Search for Tomorrow");
        if (sft == nullptr) { continue; }
        greens.insert(sft->rock_mana.green);
        reds.insert(sft->rock_mana.red);
    }
    CHECK(greens == std::set<int>{ 1 });
    CHECK(reds == std::set<int>{ 0 });
    CHECK(FindSftAndRampant(plans, 1) != nullptr);
}
