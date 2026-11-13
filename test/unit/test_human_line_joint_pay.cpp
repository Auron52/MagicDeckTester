// MTG_HUMAN_LINE_JOINT_PAY (Morph INT-14) -- TurnSolver::HumanLineJointPrepay pays the mana of EVERY step of a human
// "**" line (each hand cast's stamped cost AND each activation's own cost) in ONE solve, setting the steps' {T}
// sources aside, and pre-loads the float with exactly that bill so each step drains its own share in any order.
// USER 2026-11-13: "our mana handling should be smart enough to address shortfalls in part of the plan".
//
// Board: the scenario morph_human_seq_joint_pay_flip_first_strands_might -- Mountain, Forest, Rootbound Crag, Birds of
// Paradise; line: Den Protector's flip {1}{G}, Might of Old Krosa {G}, Might of Old Krosa {G}. The end-to-end
// commit (PrepareHumanCommit's trial, which is what engages the joint pay) is covered by that scenario and its three
// siblings; HumanPlayActive() is a process static a unit test cannot switch, so this drives the solve directly.
#include <doctest/doctest.h>

#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <string>
#include <vector>

bool TapForCostDirect(GameState& state, const ManaCost& cost_in, bool for_creature);

namespace
{
const CardDefinition& DefHljp(const std::string& name)
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

void PutHljp(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = DefHljp(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

GameState BoardHljp(bool with_birds)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    PutHljp(s, "Mountain", 11);
    PutHljp(s, "Forest", 12);
    PutHljp(s, "Rootbound Crag", 13);
    if (with_birds) { PutHljp(s, "Birds of Paradise", 14); }
    for (int k = 0; k < 2; ++k)
    {
        Card c = DefHljp("Might of Old Krosa").card;
        c.m_number = 30 + k;
        s.players[0].hand.push_back(c);
    }
    return s;
}

TurnSolver::Plan LineHljp()
{
    TurnSolver::Plan plan;
    Action flip;
    flip.kind          = Action::Kind::ActivatePermAbility;
    flip.ability_mode  = Action::AbilityMode::TurnFaceUp;
    flip.card_name     = DefHljp("Den Protector").card.m_name;
    flip.def           = &DefHljp("Den Protector");
    flip.sac_source_id = 40;
    flip.cost.generic  = 1;
    flip.cost.green    = 1;
    plan.actions.push_back(flip);
    for (int k = 0; k < 2; ++k)
    {
        const CardDefinition& d = DefHljp("Might of Old Krosa");
        Action a;
        a.kind      = Action::Kind::CastFromHand;
        a.card_name = d.card.m_name;
        a.def       = &d;
        a.cost      = d.card.m_mana_cost;
        plan.actions.push_back(a);
    }
    plan.human_sequence  = true;
    plan.human_seq_len   = 3;
    plan.human_joint_pay = true;
    return plan;
}

int UntappedHljp(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (!p.tapped) { ++n; } }
    return n;
}
}   // namespace

TEST_CASE("human line joint pay: the whole bill floats pinned, and every step drains its share in queue order")
{
    GameState s = BoardHljp(true);
    const TurnSolver::Plan plan = LineHljp();
    REQUIRE(TurnSolver::HumanLineJointPrepay(s, plan));
    CHECK(UntappedHljp(s) == 0);
    CHECK(s.floating_mana.green == 3);
    CHECK(s.floating_mana.wild == 1);
    CHECK(s.floating_mana.Total() == 4);
    // The queued order: the flip first, then both Mights -- each from the float, nothing left over.
    ManaCost flip; flip.generic = 1; flip.green = 1;
    ManaCost might = DefHljp("Might of Old Krosa").card.m_mana_cost;
    CHECK(TapForCostDirect(s, flip, false));
    CHECK(TapForCostDirect(s, might, false));
    CHECK(TapForCostDirect(s, might, false));
    CHECK(s.floating_mana.Total() == 0);
}

TEST_CASE("human line joint pay: an unpayable bill declines and leaves the board untouched")
{
    GameState s = BoardHljp(false);   // three lands for a four-mana line
    const TurnSolver::Plan plan = LineHljp();
    CHECK_FALSE(TurnSolver::HumanLineJointPrepay(s, plan));
    CHECK(UntappedHljp(s) == 3);
    CHECK(s.floating_mana.Total() == 0);
}

TEST_CASE("human line joint pay: a bill it cannot sum exactly (a snow pip) declines")
{
    GameState s = BoardHljp(true);
    TurnSolver::Plan plan = LineHljp();
    plan.actions[0].cost.snow_pips = 1;
    CHECK_FALSE(TurnSolver::HumanLineJointPrepay(s, plan));
    CHECK(UntappedHljp(s) == 4);
    CHECK(s.floating_mana.Total() == 0);
}
