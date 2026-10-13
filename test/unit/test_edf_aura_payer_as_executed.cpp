// MTG_SOLVE_AURA_PAYER_AS_EXECUTED -- MTG_SOLVE_AURA_PAYER_CHECK prices a land-Aura subset the way the
// executor pays it: the whole-turn prepay first, the per-cast walk only behind a declined prepay.
//
// The board is EDF held-out gi 1011 (seed 5301012, d0) turn 2, after the land drop: Yavimaya Coast carrying
// Wild Growth and a fresh Aether Hub ({E} 1); hand Trace of Abundance {R/W}{G}, Training Grounds {U},
// Mariposa Military Base, Overgrowth, Living Wish. {Trace, Training Grounds} is legal and the executor pays
// it: its joint prepay gives the Hub's {E} to Trace's {R/W}, the Wild Growth's {G} to Trace's {G} and the
// Coast's own face to {U}. The per-cast walk pays Trace first, takes the Coast's painless {C} for the
// surplus, and finds no {U} for Training Grounds -- so the payer check refused a line execution plays.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsAe()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const EldraziFlickerProvider& ProvAe()
{
    static const EldraziFlickerProvider p;
    return p;
}

Card PhAe(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

// The game's own card numbers (the Aura host ranking breaks ties on the lower number).
GameState Board1011()
{
    EnsureCardsAe();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 2;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = 20;
    s.players[0].energy_counters = 1;   // the Hub dropped this turn
    s.deck_has_land_aura  = true;
    s.m_provider          = &ProvAe();
    struct H { const char* name; int num; };
    for (const H& h : { H{ "Training Grounds", 51 }, H{ "Mariposa Military Base", 35 }, H{ "Overgrowth", 38 },
                        H{ "Trace of Abundance", 47 }, H{ "Living Wish", 31 } })
    { s.players[0].hand.push_back(PhAe(h.name, h.num)); }
    for (int i = 0; i < 20; ++i) { s.players[0].library.push_back(PhAe("Brushland", 300 + i)); }
    auto put = [&](const char* nm, int number, int host, bool fresh)
    {
        Permanent p;
        p.card              = CardDatabase::Instance().Lookup(nm)->card;
        p.card.m_number     = number;
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = fresh;
        p.aura_attached_to  = host;
        s.battlefield.push_back(p);
    };
    put("Yavimaya Coast", 57, 0, false);
    put("Wild Growth", 53, 57, false);
    put("Aether Hub", 3, 0, true);
    s.players[0].lands_played_this_turn = 1;
    return s;
}

TurnSolver::Plan SolveAe(const GameState& s, int arm)
{
    heurarm::t_arm[heurarm::SOLVE_AURA_PAYER_AS_EXECUTED] = static_cast<std::int8_t>(arm);
    const TurnSolver::Plan p = TurnSolver::Solve(s, /*is_pre_combat=*/true,
        TurnSolver::GreedyPermit(TurnSolver::GreedySite::D0Runner, 0));
    heurarm::t_arm[heurarm::SOLVE_AURA_PAYER_AS_EXECUTED] = -1;
    return p;
}

bool CastsAe(const TurnSolver::Plan& p, const std::string& name)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { return true; } }
    return false;
}

}   // namespace

TEST_CASE("EDF d0: the Aura payer check prices the line the way the executor pays it (MTG_SOLVE_AURA_PAYER_AS_EXECUTED)")
{
    const GameState s = Board1011();
    const TurnSolver::Plan off = SolveAe(s, 0);
    const TurnSolver::Plan on  = SolveAe(s, 1);

    CHECK_MESSAGE(!(CastsAe(off, "Trace of Abundance") && CastsAe(off, "Training Grounds")),
                  "precondition: the per-cast walk alone refuses {Trace, Training Grounds}");
    CHECK(CastsAe(on, "Trace of Abundance"));
    CHECK(CastsAe(on, "Training Grounds"));

    // ...and execution realises every cast the plan declares.
    GameState g = s;
    TurnSolver::ApplyPlan(g, on, /*is_pre_combat=*/true);
    for (const Action& a : on.actions)
    {
        if (a.kind != Action::Kind::CastFromHand) { continue; }
        bool still = false;
        for (const Card& c : g.players[0].hand) { if (c.m_name.str() == a.card_name.str()) { still = true; } }
        CHECK_MESSAGE(!still, a.card_name.str(), " was declared but not cast");
    }
}
