// GoblinsProvider::PayEchoToKeep reads staged (impulse) cards the same way in both worlds
// (MTG_ECHO_STAGED_GAS, DEFAULT ON).
//
// Echo resolves at UPKEEP, between the executor's TakeTurn passes: the executor holds a Rundvelt impulse
// card OUT of hand in Player::staged_cards there, while the rollout keeps it IN hand flagged m_is_staged.
// Clause (b) ("no gas: nothing else to cast -> pay") scanned the hand only, so the search declined Mogg
// War Marshal's echo and the real game paid it (Goblins seed 1233402 gi3401 T3: two of three Mountains
// tapped, the committed Muxus no longer castable). These pin: the two representations give the same
// answer; an expired staged card is gas in neither; the lever off reproduces the split.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <string>

namespace
{

void EnsureCardsEsg()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

// Zone cards are NAME-ONLY placeholders in real play (DeckLoader::MakePlaceholder).
Card PhEsg(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

void PutEsg(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = CardDatabase::Instance().Lookup(name)->card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

// Mogg War Marshal (not attack-lethal against 20 life) and three untapped Mountains, so the staged Goblin
// Chainwhirler ({R}{R}{R}) is castable whether its cost is read off the definition or off the name-only
// placeholder (whose empty cost reads {0}); a Mountain in hand.
GameState EchoBoard()
{
    EnsureCardsEsg();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    PutEsg(s, "Mogg War Marshal", 10);
    PutEsg(s, "Mountain", 11);
    PutEsg(s, "Mountain", 12);
    PutEsg(s, "Mountain", 13);
    s.players[0].hand.push_back(PhEsg("Mountain", 20));
    return s;
}

// The executor's upkeep view: the impulse card in staged_cards.
GameState ExecutorView(int expiry)
{
    GameState s = EchoBoard();
    s.players[0].staged_cards.push_back(StagedCard{PhEsg("Goblin Chainwhirler", 30), expiry});
    return s;
}

// The rollout's upkeep view: the same card in hand, flagged staged.
GameState RolloutView(int expiry)
{
    GameState s = EchoBoard();
    Card c = PhEsg("Goblin Chainwhirler", 30);
    c.m_is_staged     = true;
    c.m_staged_expiry = expiry;
    s.players[0].hand.push_back(c);
    return s;
}

bool Pays(const GameState& s)
{
    const Permanent* mwm = nullptr;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == "Mogg War Marshal") { mwm = &p; } }
    REQUIRE(mwm != nullptr);
    return GoblinsProvider().PayEchoToKeep(s, *mwm);
}

struct ArmEsg
{
    ArmEsg(bool on) { heurarm::t_arm[heurarm::ECHO_STAGED_GAS] = on ? 1 : 0; }
    ~ArmEsg() { heurarm::t_arm[heurarm::ECHO_STAGED_GAS] = -1; }
};

}   // namespace

TEST_CASE("Echo: an unexpired impulse card is gas in both worlds, so both decline")
{
    ArmEsg arm(true);
    CHECK_FALSE(Pays(ExecutorView(3)));
    CHECK_FALSE(Pays(RolloutView(3)));
}

TEST_CASE("Echo: an expired impulse card is gas in neither world, so both pay")
{
    ArmEsg arm(true);
    CHECK(Pays(ExecutorView(2)));
    CHECK(Pays(RolloutView(2)));
}

TEST_CASE("Echo: the lever off reproduces the executor/rollout split")
{
    ArmEsg arm(false);
    CHECK(Pays(ExecutorView(3)));         // hand = Mountain only -> "no gas" -> pay
    CHECK_FALSE(Pays(RolloutView(3)));    // the staged card is in hand -> decline
}
