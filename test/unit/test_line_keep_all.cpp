// MTG_LINE_KEEP_ALL (LineReducerKeepHold's rung on lines with NO same-turn reducer credit; ManaPayment.cpp,
// docs/design/line-keep-all-2026-11-18.md). Two boards from the mana-clean gate claude-regate-20261118 APPLY-DROP
// section, plus an inert control. In both, an earlier cast's greedy payment strands the rest of an admitted line that
// the board pays together.
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
void EnsureCardsLka()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefLka(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Permanent& PutLka(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = DefLka(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}

Card CardLka(const std::string& name, int number)
{
    Card c     = DefLka(name).card;
    c.m_number = number;
    return c;
}

struct ArmLka
{
    heurarm::Slot slot;
    ArmLka(heurarm::Slot s, bool on) : slot(s) { heurarm::t_arm[s] = on ? 1 : 0; }
    ~ArmLka() { heurarm::t_arm[slot] = -1; }
};

Action CastLka(const GameState& s, const std::string& n)
{
    Action a;
    a.kind      = Action::Kind::CastFromHand;
    a.card_name = n;
    a.def       = &DefLka(n);
    a.cost      = EffectiveSpellCost(DefLka(n), s);
    return a;
}

GameState BaseLka(int turn)
{
    EnsureCardsLka();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    return s;
}

// Pay line[pay] under the seeds the apply worlds bind (LineUnpaidCostScope + LineReducerCreditScope, the AIEngine /
// ApplyPlanDirect twin), then ask whether `rest` is still payable on the board the payment left.
bool PayThenRest(GameState& s, const std::vector<Action>& line, std::size_t pay, const ManaCost& rest,
                 bool rest_creature)
{
    LineUnpaidCostScope luc(LineCastCostTotal(line));
    const int credit = LineReducerCredit(s, line);
    const int bound = LineKeepBound(line);
    LineReducerCreditScope lrc(credit, (credit > 0 || bound) ? LineNoncreatureMv(line) : 0, bound);
    REQUIRE(TapForCostDirect(s, line[pay].cost, /*for_creature=*/false));
    return TapForCostDirect(s, rest, rest_creature);
}
}   // namespace

TEST_CASE("Line keep (all lines): Mind Stone's {2} leaves the Mountain for Atsushi's {R}{R} (Dragons s30086708 T4)")
{
    ArmLka crit_off(heurarm::LINE_KEEP_CRIT, false);   // MTG_LINE_KEEP_CRIT binds these lines too: pinned off (test_line_keep_crit.cpp)
    // Mountain#41, Mind Stone#30, Urza's Incubator#57 (Dragon) and Gruul Turf#13 in play; the line {Atsushi, Mind
    // Stone}. With the Incubator ALREADY in play there is no same-turn reducer credit: Atsushi is priced {R}{R} at
    // enumeration. The greedy pays Mind Stone (cast first) with Mind Stone + the Mountain, and Gruul Turf's {R}{G}
    // cannot pay {R}{R}. Mind Stone + Gruul Turf pay the {2} and float a {R}; the Mountain makes the second.
    GameState s0 = BaseLka(4);
    PutLka(s0, "Mountain", 41);
    PutLka(s0, "Mind Stone", 30);
    PutLka(s0, "Urza's Incubator", 57).chosen_subtype_id =
        static_cast<std::uint16_t>(SubtypeRegistry::Instance().Id("Dragon"));
    PutLka(s0, "Gruul Turf", 13);
    s0.players[0].hand.push_back(CardLka("Atsushi, the Blazing Sky", 20));
    s0.players[0].hand.push_back(CardLka("Mind Stone", 29));
    for (int k = 0; k < 20; ++k)
    { s0.players[0].library.push_back(CardLka(k % 2 ? "Mountain" : "Scourge of Valkas", 900 + k)); }
    const std::vector<Action> line = { CastLka(s0, "Atsushi, the Blazing Sky"), CastLka(s0, "Mind Stone") };
    REQUIRE(line[0].cost.generic == 0);
    REQUIRE(line[0].cost.red == 2);
    REQUIRE(line[1].cost.generic == 2);
    {
        ArmLka arm(heurarm::LINE_KEEP_ALL, true);
        CHECK(LineReducerCredit(s0, line) == 0);   // not a reducer line: the reducer rung alone never reached it
        CHECK(LineKeepBound(line));
    }
    // MTG_LINE_KEEP_ROCK (default ON) binds this Mind Stone line by itself (test_line_keep_rock.cpp); pinned off here so
    // the off arm is the line no lever reaches.
    {
        ArmLka arm(heurarm::LINE_KEEP_ALL, false);
        ArmLka rock(heurarm::LINE_KEEP_ROCK, false);
        CHECK_FALSE(LineKeepBound(line));
    }
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmLka arm(heurarm::LINE_KEEP_ALL, on);
        ArmLka rock(heurarm::LINE_KEEP_ROCK, false);
        GameState s = s0;
        CHECK(PayThenRest(s, line, 1, line[0].cost, /*rest_creature=*/true) == on);
    }
}

TEST_CASE("Line keep (all lines): Land's Edge's {1} leaves the only {U} for Treasure Hunt (Treasure Hunt s30090713 T4)")
{
    ArmLka crit_off(heurarm::LINE_KEEP_CRIT, false);   // MTG_LINE_KEEP_CRIT binds these lines too: pinned off (test_line_keep_crit.cpp)
    // Thundering Falls#53 ({U} or {R}), Cascade Bluffs#4, Sandstone Needle#37 ({R}{R}, two depletion counters) and the
    // land drop Cascade Bluffs#1; the line {Land's Edge {1}{R}{R}, Treasure Hunt {1}{U}}. The greedy pays Land's Edge
    // with the Needle's {R}{R} + Thundering Falls, and two unfed Bluffs make {C}{C}: no {U}. The Needle + one Bluffs'
    // {C} pay Land's Edge; Thundering Falls' {U} + the other Bluffs' {C} pay Treasure Hunt.
    GameState s0 = BaseLka(4);
    PutLka(s0, "Thundering Falls", 53);
    PutLka(s0, "Cascade Bluffs", 4);
    {
        Permanent& n = PutLka(s0, "Sandstone Needle", 37);
        Counter dep; dep.type = Counter::Type::Depletion; dep.count = 2;
        n.counters.Add(dep);
    }
    PutLka(s0, "Cascade Bluffs", 1);
    s0.players[0].hand.push_back(CardLka("Land's Edge", 25));
    s0.players[0].hand.push_back(CardLka("Treasure Hunt", 7));
    for (int k = 0; k < 20; ++k) { s0.players[0].library.push_back(CardLka(k % 2 ? "Island" : "Treasure Hunt", 900 + k)); }
    const std::vector<Action> line = { CastLka(s0, "Land's Edge"), CastLka(s0, "Treasure Hunt") };
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmLka arm(heurarm::LINE_KEEP_ALL, on);
        GameState s = s0;
        CHECK(PayThenRest(s, line, 0, line[1].cost, /*rest_creature=*/false) == on);
    }
}

TEST_CASE("Line keep (all lines): inert when the greedy already keeps the line, and on a one-cast line")
{
    ArmLka crit_off(heurarm::LINE_KEEP_CRIT, false);   // MTG_LINE_KEEP_CRIT binds these lines too: pinned off (test_line_keep_crit.cpp)
    // Two Mountains + Gruul Turf + Mind Stone, the same {Atsushi, Mind Stone} line: the greedy's Mind Stone + Mountain
    // leaves a Mountain and the Turf, which pay {R}{R}. The payment must be the greedy's, byte for byte, on both arms.
    GameState s0 = BaseLka(4);
    PutLka(s0, "Mountain", 41);
    PutLka(s0, "Mind Stone", 30);
    PutLka(s0, "Urza's Incubator", 57).chosen_subtype_id =
        static_cast<std::uint16_t>(SubtypeRegistry::Instance().Id("Dragon"));
    PutLka(s0, "Gruul Turf", 13);
    PutLka(s0, "Mountain", 42);
    s0.players[0].hand.push_back(CardLka("Atsushi, the Blazing Sky", 20));
    s0.players[0].hand.push_back(CardLka("Mind Stone", 29));
    for (int k = 0; k < 20; ++k)
    { s0.players[0].library.push_back(CardLka(k % 2 ? "Mountain" : "Scourge of Valkas", 900 + k)); }
    const std::vector<Action> line = { CastLka(s0, "Atsushi, the Blazing Sky"), CastLka(s0, "Mind Stone") };
    std::vector<bool> t_on, t_off;
    for (const bool on : { true, false })
    {
        ArmLka arm(heurarm::LINE_KEEP_ALL, on);
        GameState s = s0;
        CHECK(PayThenRest(s, line, 1, line[0].cost, /*rest_creature=*/true));
        for (const Permanent& p : s.battlefield) { (on ? t_on : t_off).push_back(p.tapped); }
    }
    CHECK(t_on == t_off);
    // One held cast: the line is not bound, whatever the lever says.
    ArmLka arm(heurarm::LINE_KEEP_ALL, true);
    const std::vector<Action> one = { CastLka(s0, "Mind Stone") };
    CHECK_FALSE(LineKeepBound(one));
}

