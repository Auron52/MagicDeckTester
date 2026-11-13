// Item C + I of the mana-clean program (2026-11-13): three payment / order rules that read the HAND.
//
//  * MTG_CCO_HAND_DEMAND (USER on Pirates: "the inflexible spell needs to be considered early, not late"): a WIDE
//    colored_creature_only land (Unclaimed Territory) pays a creature spell before an unrestricted dual while a
//    noncreature card in hand needs a colour (Lightning Bolt {R}), unless a hand creature needs one of its colours
//    that no unrestricted source makes. Pirates s30049037 T3: Staunch Crewmate took Spirebluff Canal, Bolt stranded.
//  * MTG_PAY_HAND_GENERIC: a generic pip paid from a float of two colours spends the one the hand needs least.
//    Martyr Lifegain s30001947 T3: Orzhov Basilica floated {W}{B}, Martyr's {1} took the {W}, Speaker {W} stranded.
//  * MTG_MARTYR_SAC_FIRST (USER: "If Martyr is already out we should for sure."): the pre-cast pass runs a Martyr out
//    since the turn began only when the plan's casts still pay after its {1}; else the trailing pass runs it.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>

namespace
{

const CardDefinition& DefHD(const std::string& name)
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

void PutHD(GameState& s, const std::string& name, int number, bool entered_this_turn = false)
{
    Permanent p;
    p.card              = DefHD(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = entered_this_turn;
    s.battlefield.push_back(p);
}

void HandHD(GameState& s, const std::string& name, int number)
{
    Card c = DefHD(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

bool TappedHD(const GameState& s, const std::string& name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { return p.tapped; } }
    FAIL("not on the battlefield: " << name);
    return false;
}

struct ArmHD
{
    heurarm::Slot slot;
    ArmHD(heurarm::Slot sl, int v) : slot(sl) { heurarm::t_arm[sl] = static_cast<std::int8_t>(v); }
    ~ArmHD() { heurarm::t_arm[slot] = -1; }
};

// Canal, Territory, Mountain untapped; Staunch Crewmate {1}{U} being paid; Lightning Bolt (+ `extra`) in hand.
GameState CrewmateBoard(const char* extra = nullptr)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    PutHD(s, "Spirebluff Canal", 49);
    PutHD(s, "Unclaimed Territory", 57);
    PutHD(s, "Mountain", 38);
    HandHD(s, "Lightning Bolt", 32);
    HandHD(s, "Staunch Crewmate", 53);
    if (extra != nullptr) { HandHD(s, extra, 70); }
    return s;
}

}   // namespace

TEST_CASE("CCO hand demand: Territory pays the creature, the dual stays up for the Bolt")
{
    const ManaCost crew = DefHD("Staunch Crewmate").card.m_mana_cost;
    {
        ArmHD on(heurarm::CCO_HAND_DEMAND, 1);
        GameState s = CrewmateBoard();
        REQUIRE(TapForCostDirect(s, crew, /*for_creature=*/true));
        CHECK(TappedHD(s, "Unclaimed Territory"));
        CHECK_FALSE(TappedHD(s, "Spirebluff Canal"));
    }
    {
        ArmHD off(heurarm::CCO_HAND_DEMAND, 0);
        GameState s = CrewmateBoard();
        REQUIRE(TapForCostDirect(s, crew, /*for_creature=*/true));
        CHECK(TappedHD(s, "Spirebluff Canal"));          // the historical order: the rainbow is held last
        CHECK_FALSE(TappedHD(s, "Unclaimed Territory"));
    }
}

TEST_CASE("CCO hand demand: no noncreature demand, or a hand creature needs the Territory's colour -> held")
{
    const ManaCost crew = DefHD("Staunch Crewmate").card.m_mana_cost;
    ArmHD on(heurarm::CCO_HAND_DEMAND, 1);
    {
        // A white creature in hand ({W}: Speaker of the Heavens) and no unrestricted white source: keep the Territory.
        GameState s = CrewmateBoard("Speaker of the Heavens");
        REQUIRE(TapForCostDirect(s, crew, /*for_creature=*/true));
        CHECK_FALSE(TappedHD(s, "Unclaimed Territory"));
    }
    {
        // No noncreature card with a coloured pip in hand: the historical order.
        GameState s = CrewmateBoard();
        s.players[0].hand.erase(s.players[0].hand.begin());   // the Bolt
        REQUIRE(TapForCostDirect(s, crew, /*for_creature=*/true));
        CHECK_FALSE(TappedHD(s, "Unclaimed Territory"));
    }
}

TEST_CASE("Pay hand generic: the {1} spends the float colour the hand does not need")
{
    auto run = [](int lever) -> GameState
    {
        ArmHD arm(heurarm::PAY_HAND_GENERIC, lever);
        GameState s;
        s.active_player_index = 0;
        s.turn_number         = 3;
        s.floating_mana.white = 1;
        s.floating_mana.black = 1;
        HandHD(s, "Speaker of the Heavens", 48);
        ManaCost one;
        one.generic = 1;
        REQUIRE(TapForCostDirect(s, one, /*for_creature=*/false));
        return s;
    };
    const GameState on = run(1);
    CHECK(on.floating_mana.white == 1);
    CHECK(on.floating_mana.black == 0);
    const GameState off = run(0);
    CHECK(off.floating_mana.white == 0);   // WUBRG
    CHECK(off.floating_mana.black == 1);
}

TEST_CASE("Martyr sac first: the pre-cast pass skips an order that does not pay, and only for a Martyr already out")
{
    static const MartyrLifegainProvider prov;
    auto board = [](int plains, bool martyr_new) -> GameState
    {
        GameState s;
        s.active_player_index = 0;
        s.turn_number         = 4;
        s.m_provider          = &prov;
        PutHD(s, "Martyr of Sands", 10, martyr_new);
        for (int i = 0; i < plains; ++i) { PutHD(s, "Plains", 20 + i); }
        HandHD(s, "Speaker of the Heavens", 48);
        return s;
    };
    auto acts = [](const GameState& s) -> std::vector<Action>
    {
        std::vector<Action> v;
        Action cast;
        cast.kind      = Action::Kind::CastFromHand;
        cast.card_name = DefHD("Speaker of the Heavens").card.m_name;
        cast.def       = &DefHD("Speaker of the Heavens");
        cast.cost      = DefHD("Speaker of the Heavens").card.m_mana_cost;
        cast.hand_index = 0;
        v.push_back(cast);
        Action sac;
        sac.kind          = Action::Kind::SacCreatureOutlet;
        sac.card_name     = DefHD("Martyr of Sands").card.m_name;
        sac.def           = &DefHD("Martyr of Sands");
        sac.cost          = DefHD("Martyr of Sands").params.sac_creature_cost.value_or(ManaCost{});
        sac.sac_source_id = 10;
        sac.chosen_x      = 1;   // position 0: before every cast
        v.push_back(sac);
        (void)s;
        return v;
    };
    ArmHD on(heurarm::MARTYR_SAC_FIRST, 1);
    {
        const GameState s = board(2, false);
        const std::vector<Action> a = acts(s);
        CHECK_FALSE(TurnSolver::RevealOutletPrecastSkip(s, a, a[1]));   // {1} then {W}: two Plains pay both
    }
    {
        const GameState s = board(1, false);
        const std::vector<Action> a = acts(s);
        CHECK(TurnSolver::RevealOutletPrecastSkip(s, a, a[1]));         // the Plains pays the {1}; Speaker cannot
    }
    {
        const GameState s = board(1, true);                              // cast this turn: the searched position
        const std::vector<Action> a = acts(s);
        CHECK_FALSE(TurnSolver::RevealOutletPrecastSkip(s, a, a[1]));
    }
    {
        ArmHD off(heurarm::MARTYR_SAC_FIRST, 0);
        const GameState s = board(1, false);
        const std::vector<Action> a = acts(s);
        CHECK_FALSE(TurnSolver::RevealOutletPrecastSkip(s, a, a[1]));
    }
}
