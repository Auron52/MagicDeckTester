// Terastodon's destroy-K on an unpinned entry (kEtbKxHeuristic -- the autonomous cast and every PUT
// route) is a PROVIDER decision (DecisionProvider::EtbDestroyK), not an engine rule: the engine
// consults the hook at resolution and destroys exactly that many of our noncreature permanents,
// making one 3/3 Elephant each. USER HARD RULE 2026-09-30 (no heuristic substitute in the engine).
// Two arms on the SAME board that MUST differ (K=0 vs K=2), so the test cannot pass because nothing
// ran; the fixture test/scenarios/stompy_terastodon_k.json covers the default projection's play.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
void EnsureCardsLoadedTK()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

struct FixedKProvider : public GenericProvider
{
    int k;
    explicit FixedKProvider(int kk) : k(kk) {}
    int EtbDestroyK(const GameState&, int, const CardDefinition&) const override { return k; }
};

GameState TerastodonBoard(const DecisionProvider* prov)
{
    GameState s;
    s.active_player_index = 0;
    s.m_provider = prov;
    int num = 100;
    auto put = [&](const char* name)
    {
        const CardDefinition* d = CardDatabase::Instance().Lookup(name);
        REQUIRE(d != nullptr);
        Permanent p;
        p.card             = d->card;
        p.card.m_number    = num++;
        p.controller_index = 0;
        p.owner_index      = 0;
        s.battlefield.push_back(p);
    };
    put("Forest"); put("Forest"); put("Forest");
    put("Terastodon");
    return s;
}

int Count(const GameState& s, bool lands)
{
    int n = 0;
    for (const Permanent& p : s.battlefield)
    {
        if (lands ? p.card.IsLand() : (p.card.IsCreature() && p.is_token)) { ++n; }
    }
    return n;
}
}   // namespace

TEST_CASE("Terastodon destroy-K: an unpinned entry destroys exactly the provider's K")
{
    EnsureCardsLoadedTK();

    FixedKProvider none(0);
    GameState a = TerastodonBoard(&none);
    FireOwnEtbTriggers(a, 0, 3, std::string(), kEtbKxHeuristic);
    CHECK(Count(a, true) == 3);
    CHECK(Count(a, false) == 0);

    FixedKProvider two(2);
    GameState b = TerastodonBoard(&two);
    FireOwnEtbTriggers(b, 0, 3, std::string(), kEtbKxHeuristic);
    CHECK(Count(b, true) == 1);
    CHECK(Count(b, false) == 2);
    CHECK(b.players[0].graveyard.size() == 2);
}

// ---- HUMAN PLAY: WHICH permanents (Landfall claude-play sweep flag 3, 2026-10-16) ----------------
// The search keeps the USER's Forests-first rule; a person is asked through the own-permanent
// board-pick (g_play_loyalty_chooser), one pick per target, over EVERY noncreature permanent. Each
// pick's default is the rule's next victim, so an unanswered prompt reproduces the autonomous loop.
namespace
{
GameState MixedTerastodonBoard()
{
    GameState s;
    s.active_player_index = 0;
    int num = 200;
    auto put = [&](const char* name, int ctrl, bool tapped)
    {
        const CardDefinition* d = CardDatabase::Instance().Lookup(name);
        REQUIRE(d != nullptr);
        Permanent p;
        p.card             = d->card;
        p.card.m_number    = num++;
        p.controller_index = ctrl;
        p.owner_index      = ctrl;
        p.tapped           = tapped;
        s.battlefield.push_back(p);
    };
    put("Forest", 0, true);              // 200 -- the rule's first victim (Forest, tapped)
    put("Forest", 0, false);             // 201
    put("Mountain", 0, false);           // 202 -- not fungible with a Forest
    put("Myriad Landscape", 0, false);   // 203
    put("Mountain", 1, false);           // 204 -- the OPPONENT's noncreature permanent
    put("Terastodon", 0, false);         // 205
    return s;
}
std::vector<int> Numbers(const GameState& s)
{
    std::vector<int> v;
    for (const Permanent& p : s.battlefield) { v.push_back(p.card.m_number); }
    return v;
}
int Elephants(const GameState& s, int ctrl)
{
    int n = 0;
    for (const Permanent& p : s.battlefield)
    { if (p.is_token && p.controller_index == ctrl && p.card.IsCreature()) { ++n; } }
    return n;
}
// Installs a loyalty chooser for the scope of one resolution; `pick` maps the offered legal set
// (battlefield indices) and the default to the reply.
struct ScopedLoyaltyChooser
{
    LoyaltyTargetChooser fn;
    LoyaltyTargetChooser* saved;
    explicit ScopedLoyaltyChooser(LoyaltyTargetChooser f) : fn(std::move(f)), saved(g_play_loyalty_chooser)
    { g_play_loyalty_chooser = &fn; }
    ~ScopedLoyaltyChooser() { g_play_loyalty_chooser = saved; }
};
}   // namespace

TEST_CASE("Terastodon victims (human): an unanswered prompt destroys exactly what the rule destroys")
{
    EnsureCardsLoadedTK();
    GameState rule = MixedTerastodonBoard();
    FireOwnEtbTriggers(rule, 0, 5, std::string(), 2);
    int asked = 0;
    GameState human = MixedTerastodonBoard();
    {
        ScopedLoyaltyChooser sc([&](const GameState&, int, const std::string&, const std::string&,
                                    const std::vector<int>& legal, int def) -> int
        {
            ++asked;
            CHECK(legal.size() >= 2);
            return def;   // the default every time
        });
        FireOwnEtbTriggers(human, 0, 5, std::string(), 2);
    }
    CHECK(asked == 2);
    CHECK(Numbers(human) == Numbers(rule));
    CHECK(human.players[0].graveyard.size() == rule.players[0].graveyard.size());
    for (std::size_t i = 0; i < rule.players[0].graveyard.size(); ++i)
    { CHECK(human.players[0].graveyard[i].m_number == rule.players[0].graveyard[i].m_number); }
    CHECK(Elephants(human, 0) == 2);
}

TEST_CASE("Terastodon victims (human): every noncreature permanent is offered, and the pick is honoured")
{
    EnsureCardsLoadedTK();
    GameState s = MixedTerastodonBoard();
    std::vector<int> offered;
    {
        ScopedLoyaltyChooser sc([&](const GameState& st, int, const std::string& src, const std::string&,
                                    const std::vector<int>& legal, int) -> int
        {
            CHECK(src == "Terastodon");
            offered.clear();
            for (int bi : legal) { offered.push_back(st.battlefield[static_cast<std::size_t>(bi)].card.m_number); }
            // Aim at the Myriad Landscape (203): a land the rule would only reach after both Forests.
            for (std::size_t li = 0; li < legal.size(); ++li)
            { if (st.battlefield[static_cast<std::size_t>(legal[li])].card.m_number == 203) { return static_cast<int>(li); } }
            return -1;
        });
        FireOwnEtbTriggers(s, 0, 5, std::string(), 1);
    }
    // Both Forests, the Mountain, Myriad Landscape AND the opponent's Mountain -- never Terastodon itself.
    CHECK(offered == std::vector<int>{ 200, 201, 202, 203, 204 });
    std::vector<int> left = Numbers(s);
    CHECK(std::find(left.begin(), left.end(), 203) == left.end());   // Myriad destroyed
    CHECK(std::find(left.begin(), left.end(), 200) != left.end());   // the rule's Forest survives
    CHECK(Elephants(s, 0) == 1);
}

TEST_CASE("Terastodon victims (human): an opponent's permanent gives the OPPONENT the Elephant")
{
    EnsureCardsLoadedTK();
    GameState s = MixedTerastodonBoard();
    {
        ScopedLoyaltyChooser sc([&](const GameState& st, int, const std::string&, const std::string&,
                                    const std::vector<int>& legal, int) -> int
        {
            for (std::size_t li = 0; li < legal.size(); ++li)
            { if (st.battlefield[static_cast<std::size_t>(legal[li])].card.m_number == 204) { return static_cast<int>(li); } }
            return -1;
        });
        FireOwnEtbTriggers(s, 0, 5, std::string(), 1);
    }
    CHECK(Elephants(s, 0) == 0);
    CHECK(Elephants(s, 1) == 1);
    CHECK(s.players[1].graveyard.size() == 1);
}

TEST_CASE("Terastodon victims (human): a forced pick is not asked")
{
    EnsureCardsLoadedTK();
    FixedKProvider none(0);
    GameState s = TerastodonBoard(&none);   // three Forests, nothing else noncreature
    int asked = 0;
    {
        ScopedLoyaltyChooser sc([&](const GameState&, int, const std::string&, const std::string&,
                                    const std::vector<int>&, int def) -> int { ++asked; return def; });
        FireOwnEtbTriggers(s, 0, 3, std::string(), 3);
    }
    CHECK(asked == 0);
    CHECK(Count(s, true) == 0);
    CHECK(Count(s, false) == 3);
}
