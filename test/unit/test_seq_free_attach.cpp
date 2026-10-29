// The subset cast walks skip a FREE attach (MTG_SEQ_FREE_ATTACH, DEFAULT ON).
//
// SubsetPayableSequential -- the rescue that admits a subset whose casts pay only in sequence (a cost
// reducer before the spell it discounts) -- refused any subset holding an action other than a hand cast
// or a Vial put, as an "unmodelled kind". Lightning Greaves' Equip {0} taps nothing and spends nothing,
// yet a subset carrying it was refused outright: Giants seed 1100023 T6, {Stinkdrinker Daredevil, Tectonic
// Giant, equip Greaves} (the Daredevil's {2} off the Giant; mana-oracle ENUM-GAP). These pin: the plan is
// on the menu and casts both with Greaves attached; the lever off keeps it off; and an equip that would
// move Greaves off a summoning-sick host whose {T} it enables is still refused.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsFa()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefFa(const std::string& name)
{
    EnsureCardsFa();
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Permanent& PutFa(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = DefFa(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}

void HandFa(GameState& s, const std::string& name, int number)
{
    Card c = DefFa(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

// Five Mountains and a Fire Diamond (six mana); Daredevil {2}{R} then Tectonic Giant {2}{R}{R} - {2} is five,
// the flat sum seven. Greaves on the battlefield, Giant Harbinger to carry it. `salt` keeps arms' boards apart.
GameState GiantsBoard(int salt)
{
    EnsureCardsFa();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 6 + salt;
    s.game_seed           = 9191 + salt;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < 8; ++k)
    {
        Card c = DefFa("Mountain").card;
        c.m_number = 200 + k;
        s.players[0].library.push_back(c);
    }
    for (int k = 0; k < 5; ++k) { PutFa(s, "Mountain", 10 + k); }
    PutFa(s, "Fire Diamond", 20);
    PutFa(s, "Lightning Greaves", 21);
    PutFa(s, "Giant Harbinger", 22);
    HandFa(s, "Stinkdrinker Daredevil", 30);
    HandFa(s, "Tectonic Giant", 31);
    return s;
}

bool CastsBothAndEquips(const TurnSolver::Plan& p)
{
    bool dare = false, giant = false, equip = false;
    for (const Action& a : p.actions)
    {
        if (a.kind == Action::Kind::Equip) { equip = true; }
        if (a.kind != Action::Kind::CastFromHand) { continue; }
        if (a.card_name.str() == "Stinkdrinker Daredevil") { dare = true; }
        if (a.card_name.str() == "Tectonic Giant")         { giant = true; }
    }
    return dare && giant && equip;
}

int CountOnBoard(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

struct ArmFa
{
    heurarm::Slot slot;
    ArmFa(heurarm::Slot s, bool on) : slot(s) { heurarm::t_arm[s] = on ? 1 : 0; }
    ~ArmFa() { heurarm::t_arm[slot] = -1; }
};

bool Offered(const GameState& s)
{
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true)) { if (CastsBothAndEquips(p)) { return true; } }
    return false;
}

}   // namespace

TEST_CASE("Free attach: Daredevil, the discounted Giant and Greaves' Equip {0} in one plan")
{
    ArmFa arm(heurarm::SEQ_FREE_ATTACH, true);
    const GameState s = GiantsBoard(0);
    const TurnSolver::Plan* line = nullptr;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    for (const TurnSolver::Plan& p : plans) { if (CastsBothAndEquips(p)) { line = &p; break; } }
    REQUIRE(line != nullptr);

    GameState r = s;
    TurnSolver::ApplyPlan(r, *line, /*is_pre_combat=*/true);
    CHECK(CountOnBoard(r, "Stinkdrinker Daredevil") == 1);
    CHECK(CountOnBoard(r, "Tectonic Giant") == 1);
    bool attached = false;
    for (const Permanent& p : r.battlefield)
    { if (p.card.m_name.str() == "Lightning Greaves" && p.equipped_to != 0) { attached = true; } }
    CHECK(attached);
}

TEST_CASE("Free attach: the lever off refuses the subset as an unmodelled kind")
{
    ArmFa arm(heurarm::SEQ_FREE_ATTACH, false);
    CHECK_FALSE(Offered(GiantsBoard(1)));
}

TEST_CASE("Free attach: moving Greaves off a summoning-sick host it lets tap is still refused")
{
    ArmFa arm(heurarm::SEQ_FREE_ATTACH, true);
    GameState s = GiantsBoard(2);
    // Greaves on a Harbinger that entered this turn: its haste is what lets the Harbinger use {T}, so an
    // equip moving it elsewhere can take mana away from a walk that never applies it. The guard keys on
    // the host alone (not on whether it makes mana), so the subset is refused as before.
    Permanent* harb = nullptr;
    Permanent* grv  = nullptr;
    for (Permanent& p : s.battlefield)
    {
        if (p.card.m_name.str() == "Giant Harbinger")   { harb = &p; }
        if (p.card.m_name.str() == "Lightning Greaves") { grv = &p; }
    }
    REQUIRE(harb != nullptr);
    REQUIRE(grv != nullptr);
    harb->entered_this_turn = true;
    grv->equipped_to        = harb->card.m_number;
    CHECK_FALSE(Offered(s));
}
