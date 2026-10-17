// MTG_HUMAN_PREPAY_DEMAND -- in HUMAN play, the whole-turn prepay of a committed multi-cast line
// (TurnSolver::BatchPrepayMainCasts, hand_demand = true from ApplyPlanDirect) holds the sources the
// REST of the hand needs for its coloured pips, if the line can pay without them.
//
// The boards are the two the Colossus Hammer claude-play sweep found (2026-10-17):
//
//   seed 2100016 T3 -- Mountain #26, Furycalm Snarl #11 (R/W, the board's only {W}), Swiftblade
//   Vindicator, and the turn's land drop Mountain #27; hand Loxodon Warhammer {3}, Sigarda's Aid {W},
//   Speaker of the Heavens {W}. The line {Colossus Hammer {1}, Magnetic Theft {R}} tapped Mountain #26
//   and the SNARL and left Mountain #27 up, so the human's next line could cast neither white card.
//
//   seed 2100017 T6 -- Plains #36/#37/#38 and Mountain #26 (the only {R}); hand Magnetic Theft x2 {R},
//   Path to Exile {W}. The line {Speaker of the Heavens {W}, Colossus Hammer {1}} paid the {1} with the
//   Mountain, two Plains idle, and no Theft was castable afterwards.
//
// Both are the joint solve's first-solution DFS taking sources in battlefield order; a one-cast line
// never reaches it (the eligible < 2 decline) and is paid by the scarcity payer's MTG_HUMAN_TAP_DEMAND
// tiebreak instead, which is why Hammer alone was paid right in both games. HumanPlayActive() is a
// process-static MTG_HUMAN_PLAY read a unit test cannot switch on, so these drive the prepay with
// hand_demand directly and pin the lever's arms through heurarm; the end-to-end human apply is the
// claude-play repro in the commit message and the reference replay sweep.
#include <doctest/doctest.h>

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

void EnsureCardsHpd()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefHpd(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

GameState FreshHpd(int turn)
{
    EnsureCardsHpd();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < 20; ++k)
    {
        Card c = DefHpd("Plains").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

// Returns the new permanent's battlefield index.
int PutHpd(GameState& s, const std::string& name, int number, bool fresh = false)
{
    Permanent p;
    p.card              = DefHpd(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = fresh;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

void HandHpd(GameState& s, const std::string& name, int number)
{
    Card c = DefHpd(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

Action CastHpd(const GameState& s, const std::string& name)
{
    const CardDefinition& d = DefHpd(name);
    Action a;
    a.kind           = Action::Kind::CastFromHand;
    a.card_name      = d.card.m_name;
    a.def            = &d;
    a.cost           = d.card.m_mana_cost;
    a.is_noncreature = !d.card.IsCreature();
    a.card_mv        = d.card.m_mana_cost.ManaValue();
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i)
    { if (s.players[0].hand[static_cast<std::size_t>(i)].m_name == d.card.m_name) { a.hand_index = i; break; } }
    return a;
}

// seed 2100016 T3, battlefield in the game's own order (the land drop already made).
struct BoardA
{
    GameState s;
    int mtn26 = -1, snarl = -1, mtn27 = -1;
};

BoardA MakeA()
{
    BoardA b;
    b.s     = FreshHpd(3);
    b.mtn26 = PutHpd(b.s, "Mountain", 26);
    b.snarl = PutHpd(b.s, "Furycalm Snarl", 11);
    PutHpd(b.s, "Swiftblade Vindicator", 57);
    b.mtn27 = PutHpd(b.s, "Mountain", 27, /*fresh=*/true);
    HandHpd(b.s, "Colossus Hammer", 5);
    HandHpd(b.s, "Loxodon Warhammer", 20);
    HandHpd(b.s, "Magnetic Theft", 22);
    HandHpd(b.s, "Sigarda's Aid", 46);
    HandHpd(b.s, "Speaker of the Heavens", 50);
    return b;
}

// seed 2100017 T6, battlefield in the game's own order.
struct BoardB
{
    GameState s;
    int pl36 = -1, mtn26 = -1, pl37 = -1, pl38 = -1;
};

BoardB MakeB()
{
    BoardB b;
    b.s     = FreshHpd(6);
    b.pl36  = PutHpd(b.s, "Plains", 36);
    PutHpd(b.s, "Dragonfire Blade", 9);
    b.mtn26 = PutHpd(b.s, "Mountain", 26);
    PutHpd(b.s, "Sigarda's Aid", 46);
    b.pl37  = PutHpd(b.s, "Plains", 37);
    PutHpd(b.s, "Forge Anew", 10);
    b.pl38  = PutHpd(b.s, "Plains", 38);
    HandHpd(b.s, "Colossus Hammer", 5);
    HandHpd(b.s, "Magnetic Theft", 22);
    HandHpd(b.s, "Magnetic Theft", 23);
    HandHpd(b.s, "Path to Exile", 34);
    HandHpd(b.s, "Speaker of the Heavens", 52);
    return b;
}

// Runs the prepay under one arm of the lever (-1 = the env default, which is ON).
bool PrepayHpd(GameState& s, const std::vector<Action>& line, bool hand_demand, int arm)
{
    heurarm::t_arm[heurarm::HUMAN_PREPAY_DEMAND] = static_cast<std::int8_t>(arm);
    const bool ok = TurnSolver::BatchPrepayMainCasts(s, line, /*mint_prefix=*/false, hand_demand);
    heurarm::t_arm[heurarm::HUMAN_PREPAY_DEMAND] = -1;
    return ok;
}

bool TappedHpd(const GameState& s, int idx) { return s.battlefield[static_cast<std::size_t>(idx)].tapped; }

}   // namespace

TEST_CASE("human prepay: {Hammer, Magnetic Theft} keeps the Snarl for the hand's white cards (seed 2100016 T3)")
{
    BoardA b = MakeA();
    const std::vector<Action> line = { CastHpd(b.s, "Colossus Hammer"), CastHpd(b.s, "Magnetic Theft") };
    REQUIRE(PrepayHpd(b.s, line, /*hand_demand=*/true, /*arm=*/-1));
    CHECK_MESSAGE(!TappedHpd(b.s, b.snarl), "the line spent the only {W} source on a generic pip");
    CHECK(TappedHpd(b.s, b.mtn26));
    CHECK(TappedHpd(b.s, b.mtn27));
    // The pre-loaded pool is exactly the line's bill: {R} pinned, the {1} as generic.
    ManaCost bill;
    bill.generic = 1;
    bill.red     = 1;
    CHECK(b.s.floating_mana.Total() == 2);
    CHECK(b.s.floating_mana.CanPay(bill));
}

TEST_CASE("human prepay: the defect's arms -- lever off, and the autonomous call, tap the Snarl (seed 2100016 T3)")
{
    // Lever off (=0): the demand-blind joint solve, byte-identical to before the fix.
    {
        BoardA b = MakeA();
        const std::vector<Action> line = { CastHpd(b.s, "Colossus Hammer"), CastHpd(b.s, "Magnetic Theft") };
        REQUIRE(PrepayHpd(b.s, line, /*hand_demand=*/true, /*arm=*/0));
        CHECK_MESSAGE(TappedHpd(b.s, b.snarl), "precondition: the lever-off arm should reproduce the defect");
        CHECK_FALSE(TappedHpd(b.s, b.mtn27));
    }
    // Autonomous / rollout / probe callers never pass the demand, whatever the lever says.
    {
        BoardA b = MakeA();
        const std::vector<Action> line = { CastHpd(b.s, "Colossus Hammer"), CastHpd(b.s, "Magnetic Theft") };
        REQUIRE(PrepayHpd(b.s, line, /*hand_demand=*/false, /*arm=*/1));
        CHECK(TappedHpd(b.s, b.snarl));
        CHECK_FALSE(TappedHpd(b.s, b.mtn27));
    }
}

TEST_CASE("human prepay: the reserve is the rest of the hand's pips, scarcest colour, narrowest source")
{
    BoardA b = MakeA();
    std::uint64_t out[64] = {};
    // The line's own casts leave the hand: Aid + Speaker owe {W}{W}, the Warhammer only generic.
    // The Snarl is the only {W} producer, so the reserve is the Snarl alone (the second {W} has none).
    const std::vector<Action> line = { CastHpd(b.s, "Colossus Hammer"), CastHpd(b.s, "Magnetic Theft") };
    REQUIRE(TurnSolver::HumanPrepayDemandHolds(b.s, line, out) == 1);
    CHECK(out[0] == (1ull << b.snarl));

    // Seed 2100017 T6: Thefts owe {R}{R} (one producer, the Mountain), Path owes {W} (three Plains).
    // Scarcest first -> the Mountain, then the lowest-numbered Plains; prefixes largest first.
    BoardB c = MakeB();
    const std::vector<Action> line_b = { CastHpd(c.s, "Speaker of the Heavens"), CastHpd(c.s, "Colossus Hammer") };
    REQUIRE(TurnSolver::HumanPrepayDemandHolds(c.s, line_b, out) == 2);
    CHECK(out[0] == ((1ull << c.mtn26) | (1ull << c.pl36)));
    CHECK(out[1] == (1ull << c.mtn26));
}

TEST_CASE("human prepay: {Speaker, Hammer} keeps the only Mountain for Magnetic Theft (seed 2100017 T6)")
{
    {
        BoardB b = MakeB();
        const std::vector<Action> line = { CastHpd(b.s, "Speaker of the Heavens"), CastHpd(b.s, "Colossus Hammer") };
        REQUIRE(PrepayHpd(b.s, line, /*hand_demand=*/true, /*arm=*/-1));
        CHECK_MESSAGE(!TappedHpd(b.s, b.mtn26), "the line spent the only {R} source on a generic pip");
        CHECK_FALSE(TappedHpd(b.s, b.pl36));   // ...and one Plains is left for Path to Exile
        CHECK(TappedHpd(b.s, b.pl37));
        CHECK(TappedHpd(b.s, b.pl38));
    }
    {
        BoardB b = MakeB();
        const std::vector<Action> line = { CastHpd(b.s, "Speaker of the Heavens"), CastHpd(b.s, "Colossus Hammer") };
        REQUIRE(PrepayHpd(b.s, line, /*hand_demand=*/true, /*arm=*/0));
        CHECK_MESSAGE(TappedHpd(b.s, b.mtn26), "precondition: the lever-off arm should reproduce the defect");
    }
}

TEST_CASE("human prepay: the hold is lossless -- a line that needs the reserved source still pays")
{
    // {Hammer {1}, Theft {R}, Sigarda's Aid {W}} is three mana off three lands: the Snarl MUST pay the
    // {W}. The reserve (the Snarl, for Speaker's {W}) cannot be held, so the ladder falls through to the
    // unrestricted solve and the line pays exactly as it did before the fix.
    BoardA b = MakeA();
    const std::vector<Action> line = { CastHpd(b.s, "Colossus Hammer"), CastHpd(b.s, "Magnetic Theft"),
                                       CastHpd(b.s, "Sigarda's Aid") };
    REQUIRE(PrepayHpd(b.s, line, /*hand_demand=*/true, /*arm=*/-1));
    CHECK(TappedHpd(b.s, b.snarl));
    CHECK(TappedHpd(b.s, b.mtn26));
    CHECK(TappedHpd(b.s, b.mtn27));
    CHECK(b.s.floating_mana.Total() == 3);
}
