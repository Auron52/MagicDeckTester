// MTG_SOLVE_UNTAP_CREDIT -- the real payer credits an ETB-untap creature's same-turn land untap, and
// Solve sends a board-gate-failing subset that casts one to that payer instead of rejecting it.
//
// The board is EDF gi 1408 (seed 1651409, d0) turn 4: Mariposa Military Base ({C}), Brushland ({G}/{W}
// or {C}), Yavimaya Coast ({G}/{U} or {C}) untapped, a Conservatory that entered tapped this turn, and
// Eldrazi Displacer. Hand: Cloud of Faeries {1}{U}, Training Grounds {U}, Fertile Ground, Emiel,
// Kitchen, Conservatory. {Cloud, Training Grounds} needs two {U} off the ONE blue land, and it is
// legal: Cloud taps the Coast, its ETB untaps it, Training Grounds taps it again (the old arm did
// exactly that and went off on turn 4). With the in-hand Fertile Ground no longer widening the colour
// gates (MTG_SOLVE_PENDING_AURA_EXACT), the subset read as {U}{U} off one source and was rejected.
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

void EnsureCardsUc()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const EldraziFlickerProvider& ProvUc()
{
    static const EldraziFlickerProvider p;
    return p;
}

Card PhUc(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

struct BpUc { std::string name; bool tapped = false; };

GameState MakeBoardUc(const std::vector<std::string>& hand, const std::vector<BpUc>& board)
{
    EnsureCardsUc();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = 20;
    s.deck_has_land_aura  = true;
    s.m_provider          = &ProvUc();
    int num = 1;
    for (const std::string& nm : hand) { s.players[0].hand.push_back(PhUc(nm, num++)); }
    for (int i = 0; i < 20; ++i) { s.players[0].library.push_back(PhUc("Brushland", num++)); }
    for (const BpUc& bp : board)
    {
        Permanent p;
        p.card              = CardDatabase::Instance().Lookup(bp.name)->card;
        p.card.m_number     = num++;
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = bp.tapped;   // the tapped land is this turn's (enters-tapped) drop
        p.tapped            = bp.tapped;
        s.battlefield.push_back(p);
    }
    s.players[0].lands_played_this_turn = 1;   // the land drop is spent: casts only
    return s;
}

TurnSolver::Plan SolveUc(const GameState& s, int arm)
{
    heurarm::t_arm[heurarm::SOLVE_UNTAP_CREDIT] = static_cast<std::int8_t>(arm);
    const TurnSolver::Plan p = TurnSolver::Solve(s, /*is_pre_combat=*/true,
        TurnSolver::GreedyPermit(TurnSolver::GreedySite::D0Runner, 0));
    heurarm::t_arm[heurarm::SOLVE_UNTAP_CREDIT] = -1;
    return p;
}

bool CastsUc(const TurnSolver::Plan& p, const std::string& name)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { return true; } }
    return false;
}

GameState Board1408()
{
    return MakeBoardUc(
        { "Emiel the Blessed", "Kitchen", "Training Grounds", "Fertile Ground", "Cloud of Faeries",
          "Conservatory" },
        { { "Mariposa Military Base" }, { "Brushland" }, { "Yavimaya Coast" },
          { "Eldrazi Displacer" }, { "Conservatory", /*tapped=*/true } });
}

}   // namespace

TEST_CASE("EDF d0: Cloud of Faeries' untap pays Training Grounds off the same blue land (MTG_SOLVE_UNTAP_CREDIT)")
{
    const GameState s = Board1408();
    const TurnSolver::Plan off = SolveUc(s, 0);
    const TurnSolver::Plan on  = SolveUc(s, 1);

    // Precondition: without the credit the pair is unpayable to Solve (two {U} pips, one {U} source).
    CHECK_FALSE_MESSAGE((CastsUc(off, "Cloud of Faeries") && CastsUc(off, "Training Grounds")),
                        "the lever-off arm should not see the untap-funded pair");
    // The fix: the pair is offered, and it is the plan.
    CHECK(CastsUc(on, "Cloud of Faeries"));
    CHECK(CastsUc(on, "Training Grounds"));
}

// The executor half of the claim: applying the chosen plan really casts both (the untap is real), so
// what Solve now admits is what execution does.
TEST_CASE("EDF d0: the untap-funded pair is what execution realises (MTG_SOLVE_UNTAP_CREDIT)")
{
    GameState s = Board1408();
    const TurnSolver::Plan on = SolveUc(s, 1);
    REQUIRE(CastsUc(on, "Training Grounds"));
    TurnSolver::ApplyPlan(s, on, /*is_pre_combat=*/true);
    bool tg = false, cloud = false;
    for (const Permanent& p : s.battlefield)
    {
        if (p.card.m_name.str() == "Training Grounds") { tg = true; }
        if (p.card.m_name.str() == "Cloud of Faeries") { cloud = true; }
    }
    CHECK(tg);
    CHECK(cloud);
}

// The untap must never pay for the creature itself: Cloud alone off a board with one land cannot be
// cast however the untap is credited (the refund arrives after the payment).
TEST_CASE("EDF d0: the untap credit cannot fund the untapper's own cost (MTG_SOLVE_UNTAP_CREDIT)")
{
    const GameState s = MakeBoardUc({ "Cloud of Faeries", "Fertile Ground" },
                                    { { "Yavimaya Coast" }, { "Eldrazi Displacer" } });
    const TurnSolver::Plan on = SolveUc(s, 1);
    CHECK_FALSE(CastsUc(on, "Cloud of Faeries"));
}
