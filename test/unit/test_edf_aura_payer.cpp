// MTG_SOLVE_AURA_PAYER_CHECK -- a land-Aura subset that PASSES Solve's board-only colour gates must still
// pass the real payer (SubsetPayableWithFilters: the Aura's declared host reserved across its payment,
// the Aura attached where the plan put it).
//
// The board is EDF gi 1475 (seed 1651476, d0) turn 7: Aether Hub with NO energy carrying Overgrowth,
// Wild Growth and Fertile Ground; Shivan Gorge, Kitchen and Mariposa Military Base untapped; two
// Peregrine Drakes. Hand: Trace of Abundance {R/W}{G}, Eldrazi Displacer {2}{W}, Eladamri's Call,
// Training Grounds (the game's own card numbers, which decide the host ranking's ties). The only {R}/{W} on the
// board is the Fertile Ground's any-colour bonus on the Hub, so the Hub pays for Trace -- and a Trace
// put ON the Hub arrives with its host already tapped, leaving Displacer's {W} without a source. The
// gates (presence + assignment over the board as it stands, the Hub credited its whole colour set)
// cannot see that, and the walk picked {Trace -> Hub, Displacer}: the executor cast Trace and dropped
// Displacer. Trace on Kitchen IS payable (Kitchen then taps for {G} plus Trace's {W}).
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

void EnsureCardsAp()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const EldraziFlickerProvider& ProvAp()
{
    static const EldraziFlickerProvider p;
    return p;
}

Card PhAp(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

struct Board1475
{
    GameState s;
    int hub = 0;
};

Board1475 Make1475()
{
    EnsureCardsAp();
    Board1475 b;
    GameState& s = b.s;
    s.active_player_index = 0;
    s.turn_number         = 7;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = 20;
    s.players[0].energy_counters = 0;   // the Hub's {E} is long spent: it makes {C} only
    s.deck_has_land_aura  = true;
    s.m_provider          = &ProvAp();
    // The game's own card numbers: the host ranking breaks yield ties on the LOWER number, so they
    // decide which second host (Kitchen #29 here) the walk is offered beside the Hub (#3).
    struct H { const char* name; int num; };
    for (const H& h : { H{ "Trace of Abundance", 47 }, H{ "Eladamri's Call", 17 },
                        H{ "Eldrazi Displacer", 200003 }, H{ "Training Grounds", 51 } })
    { s.players[0].hand.push_back(PhAp(h.name, h.num)); }
    for (int i = 0; i < 20; ++i) { s.players[0].library.push_back(PhAp("Brushland", 300 + i)); }
    auto put = [&](const char* nm, int number, int host) -> int
    {
        Permanent p;
        p.card              = CardDatabase::Instance().Lookup(nm)->card;
        p.card.m_number     = number;
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        p.aura_attached_to  = host;
        s.battlefield.push_back(p);
        return p.card.m_number;
    };
    b.hub = put("Aether Hub", 3, 0);
    put("Shivan Gorge", 46, 0);
    put("Kitchen", 29, 0);
    put("Mariposa Military Base", 35, 0);
    put("Overgrowth", 38, b.hub);
    put("Wild Growth", 53, b.hub);
    put("Peregrine Drake", 42, 0);
    put("Fertile Ground", 25, b.hub);
    put("Peregrine Drake", 43, 0);
    s.players[0].lands_played_this_turn = 1;
    return b;
}

TurnSolver::Plan SolveAp(const GameState& s, int arm)
{
    heurarm::t_arm[heurarm::SOLVE_AURA_PAYER_CHECK] = static_cast<std::int8_t>(arm);
    const TurnSolver::Plan p = TurnSolver::Solve(s, /*is_pre_combat=*/true,
        TurnSolver::GreedyPermit(TurnSolver::GreedySite::D0Runner, 0));
    heurarm::t_arm[heurarm::SOLVE_AURA_PAYER_CHECK] = -1;
    return p;
}

// Trace of Abundance's declared host in `p`, 0 when the plan does not cast it.
int TraceHost(const TurnSolver::Plan& p)
{
    for (const Action& a : p.actions)
    {
        if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Trace of Abundance")
        { return a.enchant_target; }
    }
    return 0;
}

bool CastsAp(const TurnSolver::Plan& p, const std::string& name)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { return true; } }
    return false;
}

}   // namespace

TEST_CASE("EDF d0: a land-Aura plan the gates pass still has to pay (MTG_SOLVE_AURA_PAYER_CHECK)")
{
    const Board1475 b = Make1475();
    const TurnSolver::Plan off = SolveAp(b.s, 0);
    const TurnSolver::Plan on  = SolveAp(b.s, 1);

    // Precondition: the gate-only verdict picks Trace onto the Hub that pays for it, plus Displacer.
    CHECK_MESSAGE((TraceHost(off) == b.hub && CastsAp(off, "Eldrazi Displacer")),
                  "the lever-off arm should reproduce the phantom {Trace -> Hub, Displacer}");
    // The fix: never that pair.
    CHECK_FALSE((TraceHost(on) == b.hub && CastsAp(on, "Eldrazi Displacer")));
}

// The plan the payer keeps is one execution realises: every declared cast resolves.
TEST_CASE("EDF d0: the payer-checked land-Aura plan is cast in full (MTG_SOLVE_AURA_PAYER_CHECK)")
{
    Board1475 b = Make1475();
    const TurnSolver::Plan on = SolveAp(b.s, 1);
    std::vector<std::string> cast;
    for (const Action& a : on.actions)
    { if (a.kind == Action::Kind::CastFromHand) { cast.push_back(a.card_name.str()); } }
    REQUIRE(!cast.empty());
    TurnSolver::ApplyPlan(b.s, on, /*is_pre_combat=*/true);
    // One copy of each was in hand and the library is Brushlands, so a resolved cast leaves no copy.
    for (const std::string& nm : cast)
    {
        bool still = false;
        for (const Card& c : b.s.players[0].hand) { if (c.m_name.str() == nm) { still = true; } }
        CHECK_MESSAGE(!still, nm, " was declared but not cast");
    }
}
