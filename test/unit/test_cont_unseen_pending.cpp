// MTG_CONT_UNSEEN_PENDING (heurarm CONT_UNSEEN_PENDING): a breakpoint continuation inherits the invoking line's opener
// flag (g_line_opens_unseen) only while one of that line's openers is still to come -- a copy still in hand AND its
// share still in the invoking line's unpaid bill. Dragonstorm mana-clean s30075408 T4: Apex of Power's continuation
// inherited the flag because a SPARE Apex sat in hand, the WUBRG drain spent Lotus Bloom's {B}s on generics, and
// Dragonlord Kolaghan was dropped.
#include <doctest/doctest.h>

#include "ai/EngineFlags.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <string>
#include <vector>

namespace
{
void LoadCardsCup()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Action CastCup(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    Action a;
    a.kind      = Action::Kind::CastFromHand;
    a.card_name = d->card.m_name;
    a.def       = d;
    a.cost      = d->card.m_mana_cost;
    return a;
}

GameState HandCup(const std::vector<std::string>& hand)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    for (const std::string& n : hand) { s.players[0].hand.push_back(CardDatabase::Instance().Lookup(n)->card); }
    return s;
}

struct SlotCup
{
    int slot; std::int8_t prev;
    SlotCup(int sl, bool on) : slot(sl), prev(heurarm::t_arm[sl]) { heurarm::t_arm[sl] = on ? 1 : 0; }
    ~SlotCup() { heurarm::t_arm[slot] = prev; }
};
}   // namespace

TEST_CASE("continuation opener flag: inherited only while the invoking line's opener is still owed")
{
    LoadCardsCup();
    REQUIRE(LineFloatUnseenGuardOn());
    const std::vector<Action> invoker = { CastCup("Rite of Flame"), CastCup("Seething Song"), CastCup("Apex of Power") };
    REQUIRE(CastOpensUnseen(HandCup({}), invoker.back()));
    const std::vector<Action> cont = { CastCup("Ruby Medallion"), CastCup("Dragonlord Kolaghan") };
    LineOpensUnseenScope outer(true);
    {
        // Apex resolved; a SPARE Apex is in hand, but the line's bill no longer carries an Apex share.
        SlotCup on(heurarm::CONT_UNSEEN_PENDING, true);
        LineUnpaidCostScope bill(ManaCost{});
        GameState s = HandCup({ "Apex of Power", "Dragonlord Kolaghan" });
        bool carried = true;
        CHECK_FALSE(ContinuationOpensUnseen(s, &invoker, cont, carried));
        CHECK_FALSE(carried);
    }
    {
        // The line still owes an Apex (its {7}{R}{R}{R} is in the bill) and one is in hand: still unseen.
        SlotCup on(heurarm::CONT_UNSEEN_PENDING, true);
        LineUnpaidCostScope bill(invoker.back().cost);
        GameState s = HandCup({ "Apex of Power", "Dragonlord Kolaghan" });
        bool carried = false;
        CHECK(ContinuationOpensUnseen(s, &invoker, cont, carried));
        CHECK(carried);
    }
    {
        // An inherited flag is passed on unread (the invoker is not the outermost list).
        SlotCup on(heurarm::CONT_UNSEEN_PENDING, true);
        LineUnpaidCostScope bill(ManaCost{});
        LineUnseenCarriedScope inherited(true);
        GameState s = HandCup({ "Dragonlord Kolaghan" });
        bool carried = false;
        CHECK(ContinuationOpensUnseen(s, &invoker, cont, carried));
        CHECK(carried);
    }
    {
        // The continuation's own opener still counts.
        SlotCup on(heurarm::CONT_UNSEEN_PENDING, true);
        LineUnpaidCostScope bill(ManaCost{});
        GameState s = HandCup({ "Apex of Power" });
        const std::vector<Action> cont2 = { CastCup("Apex of Power") };
        bool carried = true;
        CHECK(ContinuationOpensUnseen(s, &invoker, cont2, carried));
        CHECK_FALSE(carried);
    }
    {
        // Lever off: always inherited (the old binding).
        SlotCup off(heurarm::CONT_UNSEEN_PENDING, false);
        LineUnpaidCostScope bill(ManaCost{});
        GameState s = HandCup({ "Dragonlord Kolaghan" });
        bool carried = false;
        CHECK(ContinuationOpensUnseen(s, &invoker, cont, carried));
    }
}
