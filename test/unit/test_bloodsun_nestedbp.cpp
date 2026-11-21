// Blood Sun step 6d (docs/design/blood-sun-pipeline.md §14, step 6d): an ACQUISITION nested inside a breakpoint
// continuation. Grim Tutor's resolution is a breakpoint; its continuation casts Diabolic Revelation, whose own
// resolution is a second (nested) one. ApplyPlanDirect's deferred re-solve loop used to run ONE pass and discard the
// continuation's re-arm, so the Revelation's package was never re-decided this phase and the kill it enabled was
// unfindable at any budget. Generic: the same holds for any tutor-to-hand cast in a continuation (Demonic Tutor
// below), not for Revelation only.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/RevelationPackages.h"

#include <string>
#include <vector>

namespace
{
void EnsureCardsN()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}
Card CardN(const std::string& name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Card c = d->card;
    c.m_number = number;
    return c;
}
void PutN(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card = CardN(name, number);
    p.controller_index = 0;
    p.owner_index = 0;
    s.battlefield.push_back(p);
}
struct UnrestrictedN
{
    UnrestrictedN()  { heurarm::t_arm[heurarm::BLOODSUN_USER_PRUNE] = 0; }
    ~UnrestrictedN() { heurarm::t_arm[heurarm::BLOODSUN_USER_PRUNE] = -1; }
};
// 10 Swamps + 2 Mountains, opponent at 6: Grim Tutor (3) for Diabolic Revelation, Revelation X = 2 (7) for both
// Lightning Bolts, then both Bolts (the two Mountains) is the only this-turn kill.
GameState GrimRevBoltsN(const std::vector<std::string>& hand, const std::vector<std::string>& lib)
{
    static const BloodSunProvider bp;
    EnsureCardsN();
    GameState s;
    s.m_provider          = &bp;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 6;
    s.deck_has_landfall   = false;
    s.deck_has_revolt     = false;
    s.deck_has_land_ability_loss = true;
    s.own_death_live      = true;
    for (int k = 0; k < 10; ++k) { PutN(s, "Swamp", 10 + k); }
    for (int k = 0; k < 2; ++k)  { PutN(s, "Mountain", 30 + k); }
    for (std::size_t k = 0; k < hand.size(); ++k) { s.players[0].hand.push_back(CardN(hand[k], 1 + static_cast<int>(k))); }
    for (std::size_t k = 0; k < lib.size(); ++k) { s.players[0].library.push_back(CardN(lib[k], 100 + static_cast<int>(k))); }
    for (int k = 0; k < 8; ++k) { s.players[0].library.push_back(CardN("Swamp", 200 + k)); }
    return s;
}
}   // namespace

// The variant that takes Revelation X = 2 {Bolt, Bolt} at Grim Tutor's breakpoint (occurrence 0) now resolves the
// nested occurrence (the Revelation's) with its canon front -- both Bolts -- and kills; before the fix the apply stopped
// with the Bolts in hand and the two Mountains untapped.
TEST_CASE("nested acquisition: Grim -> Revelation {Bolt, Bolt} in the continuation, then the Bolts, in one apply")
{
    const UnrestrictedN unrestricted;
    const GameState s = GrimRevBoltsN({ "Grim Tutor" }, { "Diabolic Revelation", "Lightning Bolt", "Lightning Bolt" });
    bool found = false, killed = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        if (p.actions.size() != 1 || p.actions[0].tutor_target.str() != "Diabolic Revelation") { continue; }
        found = true;
        // The continuation list at Grim's breakpoint is ranked; walk every rank at occurrence 0.
        for (int ch = 0; ch < 16 && !killed; ++ch)
        {
            TurnSolver::Plan v = p;
            v.bp_choice = ch;
            v.bp_at     = 0;
            GameState t = s;
            TurnSolver::ApplyPlan(t, v, true);
            killed = t.players[1].life <= 0;
        }
    }
    CHECK(found);
    CHECK(killed);
}

// The search finds it: d1 FullSearchLine at an unlimited budget wins THIS turn (it returned turn 5 before the fix),
// and so does the Revelation-in-hand control (which never needed the nesting).
TEST_CASE("nested acquisition: FullSearchLine finds Grim -> Revelation -> Bolts this turn (unlimited budget)")
{
    const UnrestrictedN unrestricted;
    const GameState grim = GrimRevBoltsN({ "Grim Tutor" }, { "Diabolic Revelation", "Lightning Bolt", "Lightning Bolt" });
    CHECK(TurnSolver::FullSearchLine(grim, 1, 8, /*second_main=*/false).win_turn == grim.turn_number);
    const GameState rev = GrimRevBoltsN({ "Diabolic Revelation" }, { "Lightning Bolt", "Lightning Bolt" });
    CHECK(TurnSolver::FullSearchLine(rev, 1, 8, /*second_main=*/false).win_turn == rev.turn_number);
}

// Not Revelation-specific: Grim Tutor for Demonic Tutor, Demonic Tutor (cast in the continuation) for Lightning Bolt,
// then the Bolt (3 + 2 + 1 mana, opponent at 3). The library holds no other Bolt, so the nested tutor is the only route.
TEST_CASE("nested acquisition: Grim -> Demonic Tutor (in the continuation) -> Bolt wins this turn")
{
    const UnrestrictedN unrestricted;
    GameState s = GrimRevBoltsN({ "Grim Tutor" }, { "Demonic Tutor", "Lightning Bolt" });
    s.players[1].life = 3;
    CHECK(TurnSolver::FullSearchLine(s, 1, 8, /*second_main=*/false).win_turn == s.turn_number);
}
