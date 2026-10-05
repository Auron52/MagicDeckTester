// Unit cover for the Bruna Stage 5d claude-play sweep findings (2026-10-05, seeds 77001-77020).
// Each case pins one confirmed defect from the sweep and is written so that it FAILS on the pre-fix
// tree (the control): the sweep ledger (docs/design/analysis-Bruna.md, "## Claude-play sweep") names
// the fix commit for each.
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

void EnsureCardsBs()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefBs(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card CardBs(const std::string& name, int number)
{
    Card c     = DefBs(name).card;
    c.m_number = number;
    c.RehashName();
    return c;
}

const DecisionProvider& Bruna()
{
    static const BrunaProvider prov;
    return prov;
}

struct BoardBs
{
    GameState s;
    int next = 1;
    BoardBs()
    {
        EnsureCardsBs();
        s.active_player_index = 0;
        s.turn_number         = 4;
        s.players[0].life = s.players[1].life = gamesetup::StartingLife();
        s.m_provider = &Bruna();
        // A dead library so a cantrip / rollout draw never perturbs the board.
        for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(CardBs("Boseiju, Who Endures", 900 + k)); }
    }
    int Put(const std::string& name, bool tapped = false, bool sick = false)
    {
        Permanent p;
        p.card              = CardBs(name, next++);
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.tapped            = tapped;
        p.entered_this_turn = sick;
        s.battlefield.push_back(p);
        return p.card.m_number;
    }
    int Hand(const std::string& name) { s.players[0].hand.push_back(CardBs(name, next)); return next++; }
};

std::vector<std::string> CastNames(const TurnSolver::Plan& p)
{
    std::vector<std::string> v;
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand) { v.push_back(a.card_name.str()); } }
    return v;
}

bool PlanCasts(const TurnSolver::Plan& p, const std::string& name)
{
    for (const std::string& n : CastNames(p)) { if (n == name) { return true; } }
    return false;
}

int CountOnBf(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

}   // namespace

// ---- A-i: Somberwald Sage's creature-only surplus in a MIXED batch prepay ----------------------
// Seed 77012 T3: Avacyn's Pilgrim {G} + Lightning Greaves {2} + Mother of Runes {W} off Forest, an
// old Pilgrim, Skycloud Expanse and an old Sage. The combined (noncreature) solve cannot use the
// Sage, so the mixed two-stage solve pays the creature stage with it -- and the surplus used to land
// in the GENERAL float, spendable on Colossification.
TEST_CASE("Bruna sweep A-i: a mixed batch never launders Somberwald Sage's creature-only surplus")
{
    BoardBs b;
    b.s.players[0].lands_played_this_turn = 1;
    b.Put("Forest");
    b.Put("Avacyn's Pilgrim");
    b.Put("Skycloud Expanse");
    b.Put("Somberwald Sage");
    b.Hand("Avacyn's Pilgrim");
    b.Hand("Lightning Greaves");
    b.Hand("Mother of Runes");

    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(b.s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* all3 = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        bool equip = false;
        for (const Action& a : p.actions) { if (a.kind == Action::Kind::Equip) { equip = true; } }
        if (!equip && CastNames(p).size() == 3) { all3 = &p; break; }
    }
    REQUIRE_MESSAGE(all3 != nullptr, "the three-cast plan was not enumerated");
    GameState after = b.s;
    TurnSolver::ApplyPlan(after, *all3, /*is_pre_combat=*/true);
    CHECK(CountOnBf(after, "Avacyn's Pilgrim") == 2);
    CHECK(CountOnBf(after, "Lightning Greaves") == 1);
    CHECK(CountOnBf(after, "Mother of Runes") == 1);
    // The Sage made three of one colour; one paid a creature pip, the rest is creature-only.
    CHECK_MESSAGE(after.floating_mana.Total() == 0, "creature-only mana leaked into the general float");
    CHECK(after.floating_creature_mana.Total() == 2);
}

// ---- A-ii: a Greaves-hasted Sage's creature-only mana is not credited to a noncreature payoff ----
// Seed 77003 T4: Sage {2}{G} + Colossification {5}{G}{G} + equip Greaves -> Sage off 7 general mana.
// The haste-dork credit used to add the Sage's three units to the joint check, offering six plans
// that each drop a cast.
TEST_CASE("Bruna sweep A-ii: no plan pays a noncreature Aura with a hasted Sage's creature-only mana")
{
    BoardBs b;
    for (int k = 0; k < 6; ++k) { b.Put("Forest"); }
    b.Put("Birds of Paradise");
    b.Put("Lightning Greaves");
    b.Hand("Somberwald Sage");
    b.Hand("Colossification");
    b.Hand("Mythic Proportions");
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(b.s, /*is_pre_combat=*/true);
    for (const TurnSolver::Plan& p : plans)
    {
        const bool sage = PlanCasts(p, "Somberwald Sage");
        CHECK_MESSAGE(!(sage && PlanCasts(p, "Colossification")), "unpayable Sage + Colossification offered");
        CHECK_MESSAGE(!(sage && PlanCasts(p, "Mythic Proportions")), "unpayable Sage + Mythic Proportions offered");
    }
}

// The cap must not remove a line the restricted mana CAN fund: Sage + equip + Mother of Runes off
// three Forests -- the Forests pay the Sage, the hasted Sage's {W}{W}{W} pays Mother's {W}.
TEST_CASE("Bruna sweep A-ii: a hasted Sage still funds a creature payoff")
{
    BoardBs b;
    for (int k = 0; k < 3; ++k) { b.Put("Forest"); }
    b.Put("Lightning Greaves");
    b.Hand("Somberwald Sage");
    b.Hand("Mother of Runes");
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(b.s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* line = nullptr;
    for (const TurnSolver::Plan& p : plans)
    { if (PlanCasts(p, "Somberwald Sage") && PlanCasts(p, "Mother of Runes")) { line = &p; break; } }
    REQUIRE_MESSAGE(line != nullptr, "Sage + equip + Mother of Runes was not enumerated");
    GameState after = b.s;
    TurnSolver::ApplyPlan(after, *line, /*is_pre_combat=*/true);
    CHECK(CountOnBf(after, "Mother of Runes") == 1);
    CHECK(CountOnBf(after, "Somberwald Sage") == 1);
    CHECK(after.floating_mana.Total() == 0);
}

// ...and the SEARCH must be able to haste the Sage at all when a bigger creature is in hand: the
// width-1 haste ranking used to give Greaves only to Bruna (the attack pick), so "Sage + equip ->
// Sage + Bruna" (three lands pay the Sage, its {W}{W}{W} + three more pay Bruna) was inexpressible
// outside human play.
TEST_CASE("Bruna sweep A-iii: the search offers Greaves -> a locked mana dork beside the attack host")
{
    BoardBs b;
    for (int k = 0; k < 3; ++k) { b.Put("Forest"); }
    b.Put("Plains");
    b.Put("Plains");
    b.Put("Island");
    b.Put("Lightning Greaves");
    b.Hand("Somberwald Sage");
    b.Hand("Bruna, Light of Alabaster");
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(b.s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* line = nullptr;
    for (const TurnSolver::Plan& p : plans)
    { if (PlanCasts(p, "Somberwald Sage") && PlanCasts(p, "Bruna, Light of Alabaster")) { line = &p; break; } }
    REQUIRE_MESSAGE(line != nullptr, "Sage + equip -> Sage + Bruna was not enumerated");
    GameState after = b.s;
    TurnSolver::ApplyPlan(after, *line, /*is_pre_combat=*/true);
    CHECK(CountOnBf(after, "Bruna, Light of Alabaster") == 1);
    CHECK(CountOnBf(after, "Somberwald Sage") == 1);
}
