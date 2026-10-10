// Unit tests for the EldraziDisplacerFlicker depth-0 play defects of the 2026-10-10 discard round
// (docs/design/discard-round-play-defects-2026-10-10.md sections 3 and 4), one lever per defect; each
// section below names its lever and the game it was found in.
//
// Each case runs the lever ON (default) and OFF (heurarm override, =0) on the same hand-built board and
// asserts the two arms differ in the direction the defect names -- so every case FAILS without its fix.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

void EnsureCardsD0()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const EldraziFlickerProvider& EdfProv()
{
    static const EldraziFlickerProvider p;
    return p;
}

// Zone cards are NAME-ONLY placeholders in real play (DeckLoader::MakePlaceholder).
Card PhD0(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

// A board permanent; `host` = index of the land (in the same list) an Aura is attached to, -1 none.
struct BpD0 { std::string name; int host = -1; };

struct EdfBoard
{
    GameState s;
    std::vector<int> ids;   // battlefield m_numbers, in list order
};

EdfBoard MakeBoard(const std::vector<std::string>& hand, const std::vector<BpD0>& board,
                   const std::vector<std::string>& library, int opp_life, int energy = 0)
{
    EnsureCardsD0();
    EdfBoard b;
    GameState& s = b.s;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = opp_life;
    s.players[0].energy_counters = energy;
    s.deck_has_land_aura  = true;
    s.m_provider          = &EdfProv();
    int num = 1;
    for (const std::string& nm : hand)    { s.players[0].hand.push_back(PhD0(nm, num++)); }
    for (const std::string& nm : library) { s.players[0].library.push_back(PhD0(nm, num++)); }
    for (const BpD0& bp : board)
    {
        Permanent p;
        p.card              = CardDatabase::Instance().Lookup(bp.name)->card;
        p.card.m_number     = num++;
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        if (bp.host >= 0) { p.aura_attached_to = b.ids[static_cast<std::size_t>(bp.host)]; }
        b.ids.push_back(p.card.m_number);
        s.battlefield.push_back(p);
    }
    s.players[0].lands_played_this_turn = 1;   // the land drop is spent: casts only
    return b;
}

std::vector<std::string> Filler(int n)
{
    return std::vector<std::string>(static_cast<std::size_t>(n), "Brushland");
}

// Solve with one heurarm lever pinned (1 on, 0 off), restored on exit.
TurnSolver::Plan SolveArm(const GameState& s, heurarm::Slot slot, int arm)
{
    heurarm::t_arm[slot] = static_cast<std::int8_t>(arm);
    const TurnSolver::Plan p = TurnSolver::Solve(s, /*is_pre_combat=*/true,
        TurnSolver::GreedyPermit(TurnSolver::GreedySite::D0Runner, 0));
    heurarm::t_arm[slot] = -1;
    return p;
}

bool HasRoute(const TurnSolver::Plan& p)
{
    for (const Action& a : p.actions) { if (a.kind == Action::Kind::ComboRoute) { return true; } }
    return false;
}

bool Casts(const TurnSolver::Plan& p, const std::string& name)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { return true; } }
    return false;
}

}   // namespace

// ---- 1. MTG_SOLVE_VERIFIED_WIN ---------------------------------------------------------------------
// A live Displacer + Peregrine Drake loop and a Shivan Gorge whose {R} a Fertile Ground renews every
// iteration, opponent on 3: the COMBO OFF route's trial kills, so CollectActions offers the route (eval
// 999). The same board makes the Gorge projection call EVERY plan lethal, so a cast plan worth >= 1000
// (Drake 500 + Displacer 300 + Cloud 200) out-values the route under the old (wins, value, mask) order.
// Opponent life 3 keeps the board's blink go-off count at 3 (= kmax), so the EDF go-off cut has no
// candidate and the ranking itself decides.
TEST_CASE("EDF d0: a trial-verified COMBO OFF route outranks a projected win (MTG_SOLVE_VERIFIED_WIN)")
{
    EdfBoard b = MakeBoard(
        { "Peregrine Drake", "Eldrazi Displacer", "Cloud of Faeries" },
        { { "Yavimaya Coast" }, { "Overgrowth", 0 },
          { "Yavimaya Coast" }, { "Overgrowth", 2 },
          { "Brushland" },      { "Fertile Ground", 4 },
          { "Adarkar Wastes" }, { "Adarkar Wastes" }, { "Brushland" },
          { "Shivan Gorge" },   { "Mariposa Military Base" },
          { "Eldrazi Displacer" }, { "Peregrine Drake" } },
        Filler(20), /*opp_life=*/3);

    const TurnSolver::Plan on  = SolveArm(b.s, heurarm::SOLVE_VERIFIED_WIN, 1);
    const TurnSolver::Plan off = SolveArm(b.s, heurarm::SOLVE_VERIFIED_WIN, 0);

    // Precondition: the route is on the menu and the old order buries it under a cast plan.
    CHECK_MESSAGE(!HasRoute(off), "the old order should rank a >=1000-eval projected win above the route");
    CHECK(off.wins_this_turn);
    // The fix: the verified win is the plan.
    CHECK(HasRoute(on));
    CHECK(on.wins_this_turn);
    CHECK(on.actions.size() == 1);
}
