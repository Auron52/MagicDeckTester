// The searched dork attack's ARCHETYPE-HOLD release (MTG_DORK_ATK_PROVIDER_HOLD, kind 3) and the scored-combat
// declaration the executor's payment script pins (MTG_REAL_PAY_ATTACK_PIN, TurnSolver RolloutCombatDeclaration) --
// docs/design/mana-completeness-oracle.md 13.2.1, FiveColour s1130288 gi287.
//
// FiveColourProvider::ShouldAttackWith keeps a damage-dealing mana dork home whenever a hand card is castable off the
// full pool (Deathrite Shaman, a 1/2). The collapsed-main tower holds 0-power dorks only, so DorkAtkContestedKind left
// that hold greedy (kind 0). Kind 3 contests it; override 3 is the release the FSLineWin branch evaluates.
//
// And the gi287 detail: on the board the search scores, a {G} the engine's payment floated is gone by the declaration
// (the combat empties the pool first), so the same hold sees 1 mana and lets Deathrite swing. Declaring on the
// post-apply board as it stands -- float included -- reads 2 mana and holds it. The scored-combat pin must declare the
// way the rollout combat does.
#include <doctest/doctest.h>

#include "ai/Combat.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

std::vector<int> RolloutCombatDeclaration(GameState& state);   // TurnSolver.cpp

namespace
{

void EnsureCardsDah()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefDah(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

int PutDah(GameState& s, const std::string& name, int number, bool tapped = false)
{
    Permanent p;
    p.card             = DefDah(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    p.tapped           = tapped;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

const FiveColourProvider& FcProv()
{
    static const FiveColourProvider p;
    return p;
}

// Deathrite (fuelled: a fetchland in the graveyard) + Breeding Pool, Bloom Tender {1}{G} in hand.
GameState BoardDah(bool pool_tapped)
{
    GameState s;
    s.m_provider          = &FcProv();
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = 14;
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < 20; ++k)
    {
        Card c = DefDah("Progenitus").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    PutDah(s, "Breeding Pool", 9, pool_tapped);
    PutDah(s, "Deathrite Shaman", 13);
    {
        Card g = DefDah("Windswept Heath").card;
        g.m_number = 54;
        s.players[0].graveyard.push_back(g);
    }
    {
        Card h = DefDah("Bloom Tender").card;
        h.m_number = 16;
        s.players[0].hand.push_back(h);
    }
    return s;
}

struct ArmPinDah
{
    heurarm::Slot slot;
    std::int8_t   prev;
    ArmPinDah(heurarm::Slot s_, bool on) : slot(s_), prev(heurarm::t_arm[s_]) { heurarm::t_arm[s_] = on ? 1 : 0; }
    ~ArmPinDah() { heurarm::t_arm[slot] = prev; }
};

struct OverrideDah
{
    int prev;
    explicit OverrideDah(int v) : prev(g_dork_atk_override) { g_dork_atk_override = v; }
    ~OverrideDah() { g_dork_atk_override = prev; }
};

const Permanent& Deathrite(const GameState& s)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == 13) { return p; } }
    FAIL("no Deathrite");
    return s.battlefield.front();
}

bool Declares(const std::vector<int>& idx, const GameState& s, int number)
{
    return std::any_of(idx.begin(), idx.end(), [&](int i)
    { return s.battlefield[static_cast<std::size_t>(i)].card.m_number == number; });
}

}   // namespace

TEST_CASE("FiveColour's mana hold keeps a castable-mana Deathrite home; kind 3 contests it only under the lever")
{
    EnsureCardsDah();
    const GameState s = BoardDah(/*pool_tapped=*/false);   // Pool + Deathrite = 2: Bloom Tender castable
    const DecisionProvider& prov = ResolveProvider(s);
    REQUIRE(std::string(prov.Name()) == "FiveColour");
    CHECK_FALSE(prov.AttackWith(s, Deathrite(s)));          // the greedy hold

    {
        ArmPinDah off(heurarm::DORK_ATK_PROVIDER_HOLD, false);
        CHECK(DorkAtkContestedKind(s) == 0);                 // as before: the provider's hold stays greedy
        OverrideDah rel(3);
        CHECK_FALSE(prov.AttackWith(s, Deathrite(s)));      // override 3 is inert with the lever off
    }
    {
        ArmPinDah on(heurarm::DORK_ATK_PROVIDER_HOLD, true);
        CHECK(DorkAtkContestedKind(s) == 3);                 // searched: the release is a branch
        {
            OverrideDah rel(3);
            CHECK(prov.AttackWith(s, Deathrite(s)));         // ...and override 3 IS that release
        }
        CHECK_FALSE(prov.AttackWith(s, Deathrite(s)));      // natural again outside the scope
    }
}

TEST_CASE("Kind 3 does not fire when the provider lets the dork attack (nothing castable)")
{
    EnsureCardsDah();
    const GameState s = BoardDah(/*pool_tapped=*/true);    // Deathrite alone: 1 mana, Bloom Tender is not castable
    ArmPinDah on(heurarm::DORK_ATK_PROVIDER_HOLD, true);
    CHECK(ResolveProvider(s).AttackWith(s, Deathrite(s)));
    CHECK(DorkAtkContestedKind(s) != 3);
}

TEST_CASE("The scored-combat declaration empties the pool first, as the search's combat does (gi287)")
{
    EnsureCardsDah();
    // The engine's payment tapped Breeding Pool for a {G} it never spent: it is floating at end of main 1.
    GameState s = BoardDah(/*pool_tapped=*/true);
    s.floating_mana.Add(Color::Green, 1);
    // Declared on the board as it stands, the float counts: 2 mana, Bloom Tender is castable, Deathrite held.
    CHECK_FALSE(Declares(DeclareAttackerIndices(s), s, 13));
    // Declared the way the rollout combat declares (the pool empties at the phase boundary): Deathrite swings.
    GameState c = s;
    const std::vector<int> atk = RolloutCombatDeclaration(c);
    CHECK(Declares(atk, c, 13));
}

