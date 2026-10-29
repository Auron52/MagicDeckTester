// Unit tests for MTG_COMBAT_STABLE_SOURCES and MTG_LACKEY_DECLINE (SpellEffects.h FireCombatDamageCheatIntoPlay /
// RefindPermanentByNumber; Combat.cpp ResolveCombatDamage; TurnSolver.cpp AppendSubdecisionAxes "SEARCHED GOBLIN
// LACKEY PUT").
//
// The defect (Goblins seed 1330145 gi144, docs/design/canonical-nowin-order-dependence.md): the Lackey triggers were
// resolved off battlefield POSITIONS captured before the first put. A put of Goblin Chainwhirler kills the opponent's
// 1/1 at an EARLIER position (its ETB), every later position shifts down by one, and the second Lackey's stale
// position names a different permanent -- its trigger is silently dropped (or fires for whatever sits there now).
//
// Zone cards are NAME-ONLY placeholders, as in real play (DeckLoader::MakePlaceholder).
#include <doctest/doctest.h>

#include "ai/Combat.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{

void EnsureCardsSs()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

int g_ss_num = 9700;

Card PhSs(const std::string& name)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = g_ss_num++;
    return c;
}

int PutSs(GameState& s, const std::string& name)
{
    Permanent p;
    p.card             = CardDatabase::Instance().Lookup(name)->card;
    p.card.m_number    = g_ss_num++;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

// The opponent's 1/1 (a scheduled spawn, numbered from its own base as in GameEngine).
void PutOppOneOne(GameState& s)
{
    Card token;
    token.m_name = "1/1 Creature";
    token.RehashName();
    token.AddType(CardType::Creature);
    token.m_power     = 1;
    token.m_toughness = 1;
    token.m_number    = s.next_opp_spawn_number++;
    Permanent perm;
    perm.card             = token;
    perm.controller_index = 1;
    perm.owner_index      = 1;
    s.battlefield.push_back(perm);
}

// T4, the opponent's 1/1 at position 0, then two Goblin Lackeys able to attack; hand: Chainwhirler + Siege-Gang.
GameState WitnessBoard(std::vector<int>& lackeys)
{
    EnsureCardsSs();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    PutOppOneOne(s);                              // position 0 -- EARLIER than both Lackeys
    PutSs(s, "Mountain");
    lackeys.push_back(PutSs(s, "Goblin Lackey"));
    PutSs(s, "Mountain");
    lackeys.push_back(PutSs(s, "Goblin Lackey"));
    s.players[0].hand.push_back(PhSs("Goblin Chainwhirler"));
    s.players[0].hand.push_back(PhSs("Siege-Gang Commander"));
    for (int k = 0; k < 3; ++k) { s.players[0].library.push_back(PhSs("Mountain")); }
    return s;
}

struct ArmSs
{
    std::vector<std::pair<int, int>> saved;
    void Set(int slot, int v) { saved.emplace_back(slot, heurarm::t_arm[slot]); heurarm::t_arm[slot] = static_cast<std::int8_t>(v); }
    ~ArmSs() { for (auto it = saved.rbegin(); it != saved.rend(); ++it) { heurarm::t_arm[it->first] = static_cast<std::int8_t>(it->second); } }
};

int CountOnBoard(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.controller_index == 0 && p.card.m_name.str() == name) { ++n; } }
    return n;
}

bool OppCreatureAlive(const GameState& s)
{
    for (const Permanent& p : s.battlefield) { if (p.controller_index == 1 && p.card.IsCreature()) { return true; } }
    return false;
}

// Pin: the FIRST Lackey puts Goblin Chainwhirler (rank 1 -- the provider's front is the higher-MV Siege-Gang), the
// second takes its front (Siege-Gang, the only card left).
int ChainwhirlerFirstPin()
{
    const int r[2] = { 1, -1 };
    return LackeyPackRanks(r, 2);
}

}   // namespace

TEST_CASE("Combat stable sources: a put whose ETB removes an EARLIER permanent does not drop the next Lackey's trigger")
{
    ArmSs arm;
    std::vector<int> lk;
    GameState s = WitnessBoard(lk);
    REQUIRE(lk.size() == 2);
    s.scripted_cheat_choice = ChainwhirlerFirstPin();

    GameState fixed = s;
    FireCombatDamageCheatIntoPlay(fixed, 0, lk);
    CHECK_FALSE(OppCreatureAlive(fixed));                         // Chainwhirler's ETB killed the 1/1 at position 0
    CHECK(CountOnBoard(fixed, "Goblin Chainwhirler") == 1);       // Lackey #1's put
    CHECK(CountOnBoard(fixed, "Siege-Gang Commander") == 1);      // Lackey #2's put: its trigger FIRED
    CHECK(fixed.players[0].hand.empty());
    CHECK(fixed.scripted_cheat_choice == -1);                     // both ordinals read

    // =0: the stale positions. Lackey #2's position now names the put Chainwhirler, so its trigger is dropped and
    // Siege-Gang stays in hand -- the defect this test pins.
    arm.Set(heurarm::COMBAT_STABLE_SOURCES, 0);
    GameState stale = s;
    FireCombatDamageCheatIntoPlay(stale, 0, lk);
    CHECK(CountOnBoard(stale, "Goblin Chainwhirler") == 1);
    CHECK(CountOnBoard(stale, "Siege-Gang Commander") == 0);
    REQUIRE(stale.players[0].hand.size() == 1);
    CHECK(stale.players[0].hand[0].m_name.str() == "Siege-Gang Commander");
}

TEST_CASE("Combat stable sources: through ResolveCombatDamage, both Lackeys connect and both triggers resolve")
{
    std::vector<int> lk;
    GameState s = WitnessBoard(lk);
    s.scripted_cheat_choice = ChainwhirlerFirstPin();
    const CombatDamageResult r = ResolveCombatDamage(s, lk, /*exalted_bonus=*/0, /*collect_descs=*/true);
    CHECK(r.total_damage == 2);
    CHECK(s.players[1].life == 20 - 2 - 1);                       // two Lackeys + Chainwhirler's ETB ping
    CHECK_FALSE(OppCreatureAlive(s));
    CHECK(CountOnBoard(s, "Goblin Chainwhirler") == 1);
    CHECK(CountOnBoard(s, "Siege-Gang Commander") == 1);
    CHECK(s.players[0].hand.empty());
    CHECK(r.attacker_descs.size() == 2);
}

TEST_CASE("Combat stable sources: RefindPermanentByNumber tracks a shifted permanent and reports one that left")
{
    std::vector<int> lk;
    GameState s = WitnessBoard(lk);
    const int num = s.battlefield[static_cast<std::size_t>(lk[1])].card.m_number;
    CHECK(RefindPermanentByNumber(s, 0, num, lk[1]) == lk[1]);
    s.battlefield.erase(s.battlefield.begin());                   // the 1/1 at position 0 dies
    CHECK(RefindPermanentByNumber(s, 0, num, lk[1]) == lk[1] - 1);
    CHECK(RefindPermanentByNumber(s, 1, num, lk[1]) == -1);      // identity includes the controller
    CHECK(RefindPermanentByNumber(s, 0, 123456, lk[1]) == -1);   // gone
}

TEST_CASE("Lackey decline: the pin declines the put and keeps the card in hand")
{
    std::vector<int> lk;
    GameState s = WitnessBoard(lk);
    // Lackey #1 puts Chainwhirler, Lackey #2 DECLINES (the line gi144 only reached through the dropped trigger):
    // Siege-Gang stays in hand.
    const int r[2] = { 1, kLackeyDeclineRank };
    s.scripted_cheat_choice = LackeyPackRanks(r, 2);
    CHECK(LackeyRankAt(s.scripted_cheat_choice, 1) == kLackeyDeclineRank);
    FireCombatDamageCheatIntoPlay(s, 0, lk);
    CHECK(CountOnBoard(s, "Goblin Chainwhirler") == 1);
    CHECK(CountOnBoard(s, "Siege-Gang Commander") == 0);
    REQUIRE(s.players[0].hand.size() == 1);
    CHECK(s.players[0].hand[0].m_name.str() == "Siege-Gang Commander");
    CHECK(s.scripted_cheat_choice == -1);
}

TEST_CASE("Lackey decline: the axis carries the decline LAST, and fans a single candidate (put or decline)")
{
    ArmSs arm;
    EnsureCardsSs();
    auto board = [](const std::vector<std::string>& hand)
    {
        GameState s;
        s.active_player_index = 0;
        s.turn_number         = 4;
        s.players[0].life     = 20;
        s.players[1].life     = 20;
        s.players[0].lands_played_this_turn = 1;
        PutSs(s, "Mountain");   // casts Lightning Bolt: a plan to carry the pin (neither Goblin is castable)
        PutSs(s, "Goblin Lackey");
        for (const std::string& n : hand) { s.players[0].hand.push_back(PhSs(n)); }
        for (int k = 0; k < 3; ++k) { s.players[0].library.push_back(PhSs("Mountain")); }
        return s;
    };
    auto pins = [](const std::vector<TurnSolver::Plan>& plans)
    {
        std::set<int> out;
        for (const TurnSolver::Plan& p : plans) { if (p.lackey_choice >= 0) { out.insert(p.lackey_choice); } }
        return out;
    };
    // MTG_LACKEY_DECLINE_PRUNE off: the decline fan itself (King and Piledriver are both PLAIN, so the prune drops
    // every decline here -- checked at the end).
    arm.Set(heurarm::LACKEY_DECLINE_PRUNE, 0);
    // Two candidates (no mana to cast either): the second put rank, then the decline.
    GameState two = board({ "Goblin King", "Goblin Piledriver", "Lightning Bolt" });
    CHECK(pins(TurnSolver::EnumerateMainPlans(two, /*is_pre_combat=*/true)) == std::set<int>{ 1, kLackeyDeclineRank });
    // One candidate: put (the base plan) or decline.
    GameState one = board({ "Goblin King", "Lightning Bolt" });
    CHECK(pins(TurnSolver::EnumerateMainPlans(one, /*is_pre_combat=*/true)) == std::set<int>{ kLackeyDeclineRank });
    // Nothing to put: nothing to decline.
    GameState none = board({ "Lightning Bolt" });
    CHECK(pins(TurnSolver::EnumerateMainPlans(none, /*is_pre_combat=*/true)).empty());
    // =0: puts only, as before.
    arm.Set(heurarm::LACKEY_DECLINE, 0);
    CHECK(pins(TurnSolver::EnumerateMainPlans(two, /*is_pre_combat=*/true)) == std::set<int>{ 1 });
    CHECK(pins(TurnSolver::EnumerateMainPlans(one, /*is_pre_combat=*/true)).empty());
    // MTG_LACKEY_DECLINE_PRUNE on (the default): every candidate is plain, so putting dominates declining.
    arm.Set(heurarm::LACKEY_DECLINE, 1);
    arm.Set(heurarm::LACKEY_DECLINE_PRUNE, 1);
    CHECK(pins(TurnSolver::EnumerateMainPlans(two, /*is_pre_combat=*/true)) == std::set<int>{ 1 });
    CHECK(pins(TurnSolver::EnumerateMainPlans(one, /*is_pre_combat=*/true)).empty());
}

TEST_CASE("Lackey decline: two Lackeys fan the product of (put ranks + decline), minus the base")
{
    ArmSs arm;
    arm.Set(heurarm::LACKEY_DECLINE_PRUNE, 0);   // the fan itself; Chainwhirler + Siege-Gang are plain (pruned below)
    std::vector<int> lk;
    GameState s = WitnessBoard(lk);
    s.battlefield.erase(s.battlefield.begin());   // no opposing creature needed here
    s.players[0].hand.push_back(PhSs("Lightning Bolt"));   // a castable plan to carry the pins
    std::set<std::pair<int, int>> tuples;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        if (p.lackey_choice < 0) { continue; }
        tuples.emplace(LackeyRankAt(p.lackey_choice, 0), LackeyRankAt(p.lackey_choice, 1));
    }
    const int D = kLackeyDeclineRank;
    CHECK(tuples == std::set<std::pair<int, int>>{ { 1, 0 }, { D, 0 }, { 0, 1 }, { 1, 1 }, { D, 1 },
                                                   { 0, D }, { 1, D }, { D, D } });
    // MTG_LACKEY_DECLINE_PRUNE on: both candidates plain, so every variant with a decline goes (Rule A).
    arm.Set(heurarm::LACKEY_DECLINE_PRUNE, 1);
    tuples.clear();
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        if (p.lackey_choice < 0) { continue; }
        tuples.emplace(LackeyRankAt(p.lackey_choice, 0), LackeyRankAt(p.lackey_choice, 1));
    }
    CHECK(tuples == std::set<std::pair<int, int>>{ { 1, 0 }, { 0, 1 }, { 1, 1 } });
}
