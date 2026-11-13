// The HELD-fetch crack-then-cast hole (Spirits integrator E1, 2026-11-13;
// docs/design/held-fetch-crack-then-cast-hole.md). A Flooded Strand held on the battlefield from an
// earlier turn must be crackable to fund a cast in the SAME main: the enumerator prices those casts on
// the post-crack board (AppendHeldFetchCrackFirstPlans) and emits them with the crack in the pre-cast
// pass (Plan::land_search_act_slot 0 / the pre-cast subset) and the pick pinned (land_search_pins).
// ApplyPlan runs the same ApplyPlanDirect path the rollout uses; AIEngine::TakeTurn's pre-cast twin
// is exercised end to end by test/scenarios/spirits_held_strand_crack_then_cast.json and
// spirits_two_held_strands_crack_then_cast.json (both at --budget-ms 0).
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{
const CardDefinition& HfDef(const std::string& name)
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

void HfPut(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = HfDef(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

Card HfCard(const std::string& name, int number)
{
    Card c     = HfDef(name).card;
    c.m_number = number;
    return c;
}

// Main 1 of turn 3, land drop used: `lands` on the battlefield (held from earlier turns), Drogskol
// Captain ({1}{W}{U}) in hand, a library of Plains with exactly ONE Island.
GameState HfBoard(const std::vector<std::string>& lands)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    int n = 1;
    for (const std::string& l : lands) { HfPut(s, l, n++); }
    s.players[0].hand.push_back(HfCard("Drogskol Captain", 50));
    s.players[0].library.push_back(HfCard("Island", 60));
    for (int i = 0; i < 10; ++i) { s.players[0].library.push_back(HfCard("Plains", 61 + i)); }
    return s;
}

int CountCracks(const TurnSolver::Plan& p)
{
    int n = 0;
    for (const Action& a : p.actions) { if (TurnSolver::IsLandSearchActivation(a)) { ++n; } }
    return n;
}

bool CastsCaptain(const TurnSolver::Plan& p)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name == "Drogskol Captain") { return true; } }
    return false;
}

bool OnBattlefield(const GameState& s, const std::string& name)
{
    for (const Permanent& q : s.battlefield) { if (q.card.m_name == name) { return true; } }
    return false;
}

// Every enumerated plan that casts the Captain, applied: does one of them resolve it with an Island
// found by `cracks` Strand crack(s)?
bool SomeCrackFirstPlanCastsCaptain(const GameState& s, int cracks)
{
    bool found = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        if (!CastsCaptain(p)) { continue; }
        // The only way to the Captain's {U}: the crack(s) run BEFORE the cast.
        CHECK(CountCracks(p) >= 1);
        CHECK(p.land_search_act_slot != TurnSolver::kLandSlotAfterCasts);
        GameState g = s;
        TurnSolver::ApplyPlan(g, p, /*is_pre_combat=*/true);
        if (!OnBattlefield(g, "Drogskol Captain") || !OnBattlefield(g, "Island")) { continue; }
        if (CountCracks(p) != cracks) { continue; }
        CHECK(g.players[0].life == gamesetup::StartingLife() - cracks);   // each crack paid 1 life
        CHECK_FALSE(OnBattlefield(g, "Flooded Strand"));
        found = true;
    }
    return found;
}
}   // namespace

TEST_CASE("Held Flooded Strand: crack it for the Island, THEN cast Drogskol Captain, in one main")
{
    GameState s = HfBoard({"Plains", "Plains", "Flooded Strand"});
    const SpiritsProvider prov;
    s.m_provider = &prov;
    // Before the fix no enumerated plan cast the Captain at all (its {U} only exists after the crack).
    CHECK(SomeCrackFirstPlanCastsCaptain(s, 1));
}

TEST_CASE("Two held Flooded Strands: BOTH cracks may precede the cast (the crack-first recursion)")
{
    GameState s = HfBoard({"Plains", "Flooded Strand", "Flooded Strand"});
    const SpiritsProvider prov;
    s.m_provider = &prov;
    CHECK(SomeCrackFirstPlanCastsCaptain(s, 2));
}

TEST_CASE("Held Strand with nothing to fund: no crack-first plan is emitted for an empty post-crack plan")
{
    // The Captain is castable without the crack's land (an Island is already out): the crack-first
    // plans exist (the crack may still precede it), but none is an action-less crack -- that line is the
    // trailing-crack plan, emitted once by the base enumeration (identity fold).
    GameState s = HfBoard({"Plains", "Island", "Plains", "Flooded Strand"});
    const SpiritsProvider prov;
    s.m_provider = &prov;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        if (p.land_search_act_slot == TurnSolver::kLandSlotFirst && CountCracks(p) == static_cast<int>(p.actions.size()))
        { CHECK_MESSAGE(false, "a crack-only plan carries the pre-cast slot"); }
    }
}
