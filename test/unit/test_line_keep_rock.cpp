// MTG_LINE_KEEP_ROCK (the line-keep rung on lines that CAST A MANA ROCK, with the rest of the line judged on a board
// that holds the rock being paid for; LineReducerKeepHold, ManaPayment.cpp; docs/design/rock-in-plan-2026-11-19.md).
// A rock in the line declines the whole-turn joint prepay (PP_PRODUCER), so the per-cast greedy pays the line with no
// joint allocation. Boards from the mana-clean gate's APPLY-DROP sections (claude-queue-20261119b / -20261119g2).
#include <doctest/doctest.h>

#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <cstdint>
#include <string>
#include <vector>

namespace
{
void EnsureCardsLkr()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefLkr(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Permanent& PutLkr(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = DefLkr(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}

Card CardLkr(const std::string& name, int number)
{
    Card c     = DefLkr(name).card;
    c.m_number = number;
    return c;
}

struct ArmLkr
{
    heurarm::Slot slot;
    ArmLkr(heurarm::Slot s, bool on) : slot(s) { heurarm::t_arm[s] = on ? 1 : 0; }
    ~ArmLkr() { heurarm::t_arm[slot] = -1; }
};

Action CastLkr(const GameState& s, const std::string& n)
{
    Action a;
    a.kind      = Action::Kind::CastFromHand;
    a.card_name = n;
    a.def       = &DefLkr(n);
    a.cost      = EffectiveSpellCost(DefLkr(n), s);
    return a;
}

GameState BaseLkr(int turn)
{
    EnsureCardsLkr();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    return s;
}

// Pay the rock line[rock] under the seeds the apply worlds bind (LineUnpaidCostScope + LineReducerCreditScope, the
// AIEngine / ApplyPlanDirect twin) with the rock as the spell being paid for (SpellSubtypePayScope, as the cast site
// binds it); the rock then resolves (enters untapped) and `rest` is paid on the board that leaves.
bool PayRockThenRest(GameState& s, const std::vector<Action>& line, std::size_t rock, const Card& rock_card,
                     const ManaCost& rest, bool rest_creature)
{
    LineUnpaidCostScope luc(LineCastCostTotal(line));
    const int credit = LineReducerCredit(s, line);
    const bool bound = LineKeepBound(line);
    LineReducerCreditScope lrc(credit, (credit > 0 || bound) ? LineNoncreatureMv(line) : 0, bound);
    {
        SpellSubtypePayScope paying(&rock_card);
        REQUIRE(TapForCostDirect(s, line[rock].cost, /*for_creature=*/false));
    }
    Permanent p;
    p.card             = rock_card;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
    return TapForCostDirect(s, rest, rest_creature);
}

// Dragons s30094937 T4: Mountain#32, Mind Stone#29, Gruul Turf#13 ({T}: {R}{G}); the line {Dragonspeaker Shaman {1}{R}{R},
// Mind Stone {2}}, Mind Stone first. 5 mana with the new Mind Stone, a 5-mana line: the only payment floats the Turf's
// {R} off Mind Stone's {2} (the Turf's {G} + the old Mind Stone's {C}), and the Shaman takes it + the Mountain's {R} +
// the NEW Mind Stone's {C}. The rest is payable only with the rock on the board.
GameState ShamanBoard()
{
    GameState s0 = BaseLkr(4);
    PutLkr(s0, "Mountain", 32);
    PutLkr(s0, "Mind Stone", 29);
    PutLkr(s0, "Gruul Turf", 13);
    s0.players[0].hand.push_back(CardLkr("Dragonspeaker Shaman", 8));
    s0.players[0].hand.push_back(CardLkr("Mind Stone", 30));
    for (int k = 0; k < 20; ++k) { s0.players[0].library.push_back(CardLkr(k % 2 ? "Mountain" : "Glorybringer", 900 + k)); }
    return s0;
}
}   // namespace

TEST_CASE("Line keep (rock): Mind Stone's {2} floats the Turf's {R} for Dragonspeaker Shaman (Dragons s30094937 T4)")
{
    const GameState s0 = ShamanBoard();
    const std::vector<Action> line = { CastLkr(s0, "Dragonspeaker Shaman"), CastLkr(s0, "Mind Stone") };
    REQUIRE(line[0].cost.red == 2);
    REQUIRE(line[0].cost.generic == 1);
    REQUIRE(line[1].cost.generic == 2);
    {
        ArmLkr arm(heurarm::LINE_KEEP_ROCK, true);
        CHECK(LineReducerCredit(s0, line) == 0);   // the Shaman discounts Dragons; neither cast here is one
        CHECK(LineKeepBound(line));
    }
    {
        ArmLkr arm(heurarm::LINE_KEEP_ROCK, false);
        CHECK_FALSE(LineKeepBound(line));
    }
    const Card rock = CardLkr("Mind Stone", 30);
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmLkr arm(heurarm::LINE_KEEP_ROCK, on);
        GameState s = s0;
        CHECK(PayRockThenRest(s, line, 1, rock, line[0].cost, /*rest_creature=*/true) == on);
    }
}

TEST_CASE("Line keep (rock): the rock credit is what fires it -- MTG_LINE_KEEP_ALL alone judges the rest without it")
{
    // The same board under MTG_LINE_KEEP_ALL (every multi-cast line bound) but the rock lever off: the rest {1}{R}{R} is
    // judged on the board after Mind Stone's payment WITHOUT the new Mind Stone, where no payment of the {2} leaves it
    // payable (4 mana for a 5-mana line), so the rung cannot fire and the Shaman is stranded (the documented residual
    // of docs/design/line-keep-all-2026-11-18.md).
    const GameState s0 = ShamanBoard();
    const std::vector<Action> line = { CastLkr(s0, "Dragonspeaker Shaman"), CastLkr(s0, "Mind Stone") };
    const Card rock = CardLkr("Mind Stone", 30);
    ArmLkr all(heurarm::LINE_KEEP_ALL, true);
    ArmLkr arm(heurarm::LINE_KEEP_ROCK, false);
    GameState s = s0;
    CHECK_FALSE(PayRockThenRest(s, line, 1, rock, line[0].cost, /*rest_creature=*/true));
}

TEST_CASE("Line keep (rock): inert when the greedy already keeps the line, and on a line with no rock")
{
    // A second Mountain: whatever pays Mind Stone's {2}, a Mountain + the Turf or two Mountains are left for {R}{R}.
    // The payment must be the greedy's, byte for byte, on both arms.
    GameState s0 = ShamanBoard();
    PutLkr(s0, "Mountain", 33);
    const std::vector<Action> line = { CastLkr(s0, "Dragonspeaker Shaman"), CastLkr(s0, "Mind Stone") };
    const Card rock = CardLkr("Mind Stone", 30);
    std::vector<bool> t_on, t_off;
    for (const bool on : { true, false })
    {
        ArmLkr arm(heurarm::LINE_KEEP_ROCK, on);
        GameState s = s0;
        CHECK(PayRockThenRest(s, line, 1, rock, line[0].cost, /*rest_creature=*/true));
        for (const Permanent& p : s.battlefield) { (on ? t_on : t_off).push_back(p.tapped); }
    }
    CHECK(t_on == t_off);
    // No rock in the line: not bound by this lever (MTG_LINE_KEEP_ALL's business).
    ArmLkr arm(heurarm::LINE_KEEP_ROCK, true);
    s0.players[0].hand.push_back(CardLkr("Glorybringer", 31));
    const std::vector<Action> norock = { CastLkr(s0, "Dragonspeaker Shaman"), CastLkr(s0, "Glorybringer") };
    CHECK_FALSE(LineKeepBound(norock));
}

TEST_CASE("Line keep (rock): MTG_LKR_PLAYOUT_OFF (default ON) -- no rung inside a horizon playout, the full rung elsewhere")
{
    // The Shaman board of the first case: only the rung's float keeps the line. Inside a horizon playout the adopted
    // default (USER 2026-11-24) pays it with the greedy, which strands the Shaman; outside a playout, and with the lever
    // =0 inside one, the rung fires exactly as before.
    const GameState s0 = ShamanBoard();
    const std::vector<Action> line = { CastLkr(s0, "Dragonspeaker Shaman"), CastLkr(s0, "Mind Stone") };
    const Card rock = CardLkr("Mind Stone", 30);
    ArmLkr arm(heurarm::LINE_KEEP_ROCK, true);
    struct PlayoutNest
    {
        PlayoutNest() { TurnSolver::RolloutNestAdjustForTest(+1); }
        ~PlayoutNest() { TurnSolver::RolloutNestAdjustForTest(-1); }
    };
    REQUIRE(heurarm::t_arm[heurarm::LKR_PLAYOUT_OFF] == -1);   // the env default, not a pinned arm
    {
        GameState s = s0;
        CHECK_FALSE(TurnSolver::InRolloutPlayout());
        CHECK(PayRockThenRest(s, line, 1, rock, line[0].cost, /*rest_creature=*/true));
    }
    {
        PlayoutNest nest;
        REQUIRE(TurnSolver::InRolloutPlayout());
        GameState s = s0;
        CHECK_FALSE(PayRockThenRest(s, line, 1, rock, line[0].cost, /*rest_creature=*/true));
    }
    {
        PlayoutNest nest;
        ArmLkr off(heurarm::LKR_PLAYOUT_OFF, false);
        GameState s = s0;
        CHECK(PayRockThenRest(s, line, 1, rock, line[0].cost, /*rest_creature=*/true));
    }
    CHECK_FALSE(TurnSolver::InRolloutPlayout());
}
