// Unit cover for MTG_RESTRICTOR_ORDER_EXACT, the d0-runner stall behind Hinata2's 2HG gi 254 and d0 gi 195 /
// 865 / 1773 (docs/design/reality-spasm-crackle-classification.md, "The d0-runner stall"): the subset walks'
// Irencrag Feat test ("you can cast only one more spell this turn") also requires the casts ranked before it,
// and Irencrag itself, to be payable without its own float -- otherwise every payable order casts it earlier
// and one more cast follows -- and rejects a subset in which no order can pay Irencrag at all.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsRo()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefRo(const std::string& name)
{
    EnsureCardsRo();
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

struct PinRo
{
    int slot; std::int8_t prev;
    PinRo(int sl, bool on) : slot(sl), prev(heurarm::t_arm[sl]) { heurarm::t_arm[sl] = on ? 1 : 0; }
    ~PinRo() { heurarm::t_arm[slot] = prev; }
};

// `board` on our side (untapped, not summoning sick), `hand` in hand, land drop spent, pre-combat.
// `salt` keeps the per-thread Solve memo (keyed on the state) from serving one arm to the other.
GameState BoardRo(const std::vector<std::string>& board, const std::vector<std::string>& hand,
                  int opp_life, int salt)
{
    EnsureCardsRo();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = 20 + salt;
    s.players[1].life     = opp_life;
    int num = 1;
    for (const std::string& n : board)
    {
        Permanent p;
        p.card              = DefRo(n).card;
        p.card.m_number     = num++;
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        s.battlefield.push_back(p);
    }
    s.players[0].lands_played_this_turn = 1;
    for (const std::string& n : hand) { Card c = DefRo(n).card; c.m_number = num++; s.players[0].hand.push_back(c); }
    for (int k = 0; k < 10; ++k) { Card c = DefRo("Island").card; c.m_number = 900 + k; s.players[0].library.push_back(c); }
    return s;
}

bool CastsAll(const TurnSolver::Plan& p, const std::vector<std::string>& names)
{
    for (const std::string& n : names)
    {
        bool found = false;
        for (const Action& a : p.actions)
        { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == n) { found = true; break; } }
        if (!found) { return false; }
    }
    return true;
}

bool AnyPlanCastsAll(const GameState& s, const std::vector<std::string>& names)
{
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    { if (CastsAll(p, names)) { return true; } }
    return false;
}

TurnSolver::Plan GreedyRo(const GameState& s)
{
    return TurnSolver::Solve(s, true, TurnSolver::GreedyPermit(TurnSolver::GreedySite::HorizonLeaf, 0));
}

const char* kIren  = "Irencrag Feat";
const char* kOrni  = "Ornithopter of Paradise";
const char* kCrk   = "Crackle with Power";

}   // namespace

TEST_CASE("RESTRICTOR_ORDER_EXACT: Irencrag + Ornithopter + Crackle on four mana is payable only Irencrag-first")
{
    const HinataProvider hp;
    // Four Mountains: Irencrag ({1}{R}{R}{R}) takes all four, so the Ornithopter ({2}) -- ranked BEFORE
    // Irencrag -- can only be paid from Irencrag's float, i.e. after him, which uses his one remaining
    // cast and strands the Crackle. Opponent at 5 so Crackle X=1 wins (the hold never applies).
    GameState probe = BoardRo({ "Mountain", "Mountain", "Mountain", "Mountain" }, { kIren, kOrni, kCrk }, 5, 0);
    probe.m_provider = &hp;
    REQUIRE(hp.CastOrderRank(probe, DefRo(kOrni)) <= hp.CastOrderRank(probe, DefRo(kIren)));
    REQUIRE(hp.CastOrderRank(probe, DefRo(kCrk))  >  hp.CastOrderRank(probe, DefRo(kIren)));
    {
        PinRo off(heurarm::RESTRICTOR_ORDER_EXACT, false);
        GameState s = BoardRo({ "Mountain", "Mountain", "Mountain", "Mountain" }, { kIren, kOrni, kCrk }, 5, 1);
        s.m_provider = &hp;
        CHECK(AnyPlanCastsAll(s, { kIren, kOrni, kCrk }));         // the unrealisable line is offered
    }
    {
        PinRo on(heurarm::RESTRICTOR_ORDER_EXACT, true);
        GameState s = BoardRo({ "Mountain", "Mountain", "Mountain", "Mountain" }, { kIren, kOrni, kCrk }, 5, 2);
        s.m_provider = &hp;
        CHECK_FALSE(AnyPlanCastsAll(s, { kIren, kOrni, kCrk }));
        CHECK(AnyPlanCastsAll(s, { kIren, kCrk }));                 // the realisable kill stays
        CHECK_FALSE(CastsAll(GreedyRo(s), { kIren, kOrni, kCrk }));
        CHECK(CastsAll(GreedyRo(s), { kIren, kCrk }));
    }
    // Seven Mountains: the Ornithopter ({2}) and Irencrag ({1}{R}{R}{R}) are both paid from the board in
    // rank order, and the float funds the Crackle (X=2, the provider's max X) -- legal under both arms.
    for (bool arm : { true, false })
    {
        PinRo pin(heurarm::RESTRICTOR_ORDER_EXACT, arm);
        GameState s = BoardRo({ "Mountain", "Mountain", "Mountain", "Mountain", "Mountain", "Mountain",
                                "Mountain" },
                              { kIren, kOrni, kCrk }, 5, arm ? 3 : 4);
        s.m_provider = &hp;
        CHECK(AnyPlanCastsAll(s, { kIren, kOrni, kCrk }));
    }
}

TEST_CASE("RESTRICTOR_ORDER_EXACT: Irencrag's own {R}{R}{R} cannot be paid by his own float (2HG gi 254)")
{
    const HinataProvider hp;
    // Sol Ring + Izzet Boilerworks + Forbidden Orchard: five mana, but only TWO can be red. The flat pool
    // books the bounce land as two wild, and the ritual credit (seven red) used to cover Irencrag's own
    // pips. Opponent at 10 so the Crackle the float funds (X=2) wins -- the hold never applies.
    {
        PinRo off(heurarm::RESTRICTOR_ORDER_EXACT, false);
        GameState s = BoardRo({ "Sol Ring", "Izzet Boilerworks", "Forbidden Orchard" }, { kIren, kCrk }, 10, 5);
        s.m_provider = &hp;
        CHECK(AnyPlanCastsAll(s, { kIren, kCrk }));                 // the phantom line
    }
    {
        PinRo on(heurarm::RESTRICTOR_ORDER_EXACT, true);
        GameState s = BoardRo({ "Sol Ring", "Izzet Boilerworks", "Forbidden Orchard" }, { kIren, kCrk }, 10, 6);
        s.m_provider = &hp;
        CHECK_FALSE(AnyPlanCastsAll(s, { kIren, kCrk }));
        CHECK_FALSE(CastsAll(GreedyRo(s), { kIren, kCrk }));
    }
    // ...and with NO follower in rank order (Magma Opus ranks before Irencrag): the casts-after count is 0,
    // so only the SELF test can see that no order pays his {R}{R}{R}. Opponent at 4 so the Opus wins (the
    // waste gate's lethal exemption admits the subset under the old model).
    {
        PinRo off(heurarm::RESTRICTOR_ORDER_EXACT, false);
        GameState s = BoardRo({ "Sol Ring", "Izzet Boilerworks", "Forbidden Orchard" }, { kIren, "Magma Opus" }, 4, 9);
        s.m_provider = &hp;
        CHECK(AnyPlanCastsAll(s, { kIren, "Magma Opus" }));
    }
    {
        PinRo on(heurarm::RESTRICTOR_ORDER_EXACT, true);
        GameState s = BoardRo({ "Sol Ring", "Izzet Boilerworks", "Forbidden Orchard" }, { kIren, "Magma Opus" }, 4, 10);
        s.m_provider = &hp;
        CHECK_FALSE(AnyPlanCastsAll(s, { kIren, "Magma Opus" }));
    }
    // A third red source makes him payable from the board: the line is legal under both arms.
    for (bool arm : { true, false })
    {
        PinRo pin(heurarm::RESTRICTOR_ORDER_EXACT, arm);
        GameState s = BoardRo({ "Sol Ring", "Izzet Boilerworks", "Forbidden Orchard", "Mountain" },
                              { kIren, kCrk }, 10, arm ? 7 : 8);
        s.m_provider = &hp;
        CHECK(AnyPlanCastsAll(s, { kIren, kCrk }));
    }
}
