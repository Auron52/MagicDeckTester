// Garth One-Eye's conjured Black Lotus, cracked in the plan that conjures it (USER 2026-10-28: "We should
// be allowing garth to produce black lotus for mana for sure").
//
// Garth's {T} conjures a Black Lotus and casts it as the ability resolves. The Lotus is a noncreature
// artifact, so it can pay its own {T} at once, and "{T}, Sacrifice: add three mana of any one colour" is a
// mana ability. The engine only ever emitted a SacForMana for a Lotus ALREADY on the battlefield, so a
// plan could conjure the Lotus but never spend it: {Garth -> Lotus, crack, the casts it pays for} was
// inexpressible at any budget (mana oracle ENUM-GAP, FiveColour seed 1100028 gi27 T6). The fix is a
// conjure-and-crack GarthActivate (ritual_float = 3) beside the conjure-and-keep one. These pin:
//   1. ApplyGarthActivate with a crack colour lands the copy, sacrifices it and floats three of that
//      colour; without one the Lotus stays (the keep variant, unchanged);
//   2. the menu offers both variants, and only the crack funds a cast the board cannot otherwise pay;
//   3. the rollout (ApplyPlan) and the executor (TakeTurn) both cast off the cracked Lotus -- lockstep;
//   4. the colour is searched (one variant per colour) and credited exactly: a {G} creature is paid only
//      by the green crack, and the other colours never reach the menu with it.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "mtg_test_seam.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsGl()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefGl(const std::string& name)
{
    EnsureCardsGl();
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

void PutGl(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = DefGl(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

void HandGl(GameState& s, const std::string& name, int number)
{
    Card c = DefGl(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

const FiveColourProvider& FcProvGl()
{
    static const FiveColourProvider p;
    return p;
}

GameState FreshGl()
{
    EnsureCardsGl();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.game_seed           = 9393;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;   // no land drop: the Lotus is the only mana
    s.m_provider          = &FcProvGl();
    for (int k = 0; k < 8; ++k)
    {
        Card c = DefGl("Lightning Bolt").card;   // nothing drawn mid-test can be cast
        c.m_number = 200 + k;
        s.players[0].library.push_back(c);
    }
    PutGl(s, "Garth One-Eye", 30);
    return s;
}

int CountGl(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

const Permanent* GarthGl(const GameState& s)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == 30) { return &p; } }
    return nullptr;
}

bool CastsGl(const TurnSolver::Plan& p, const std::string& name)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { return true; } }
    return false;
}

const Action* GarthLotusGl(const TurnSolver::Plan& p)
{
    for (const Action& a : p.actions)
    {
        if (a.kind == Action::Kind::GarthActivate && a.tutor_target.str() == "Black Lotus") { return &a; }
    }
    return nullptr;
}

void ResolveStackGl(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

GameState RunExecutorGl(const GameState& s, const TurnSolver::Plan& plan)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, plan, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, ResolveStackGl);
    return exec;
}

// The plan on `s`'s menu that conjures-and-cracks the Lotus and casts `name`, or nullptr.
const TurnSolver::Plan* CrackPlanCasting(const std::vector<TurnSolver::Plan>& plans, const std::string& name)
{
    for (const TurnSolver::Plan& p : plans)
    {
        const Action* g = GarthLotusGl(p);
        if (g && TurnSolver::IsGarthLotusCrack(*g) && CastsGl(p, name)) { return &p; }
    }
    return nullptr;
}

}   // namespace

TEST_CASE("Garth conjure-and-crack: the copy lands, is sacrificed, and floats three of the colour")
{
    GameState s = FreshGl();
    ApplyGarthActivate(s, 0, 30, "Black Lotus", 0, "G");
    REQUIRE(GarthGl(s) != nullptr);
    CHECK(GarthGl(s)->tapped);
    CHECK((GarthGl(s)->garth_chosen_mask & (1u << 5)) != 0);   // Black Lotus is chosen for the game
    CHECK(s.spells_cast_this_turn == 1);                        // the copy was CAST
    CHECK(CountGl(s, "Black Lotus") == 0);                      // ... and sacrificed
    CHECK(s.floating_mana.green == 3);

    // No crack colour: the keep variant, exactly as before -- the Lotus stays, nothing floats.
    GameState t = FreshGl();
    ApplyGarthActivate(t, 0, 30, "Black Lotus", 0);
    CHECK(CountGl(t, "Black Lotus") == 1);
    CHECK(t.floating_mana.Total() == 0);
}

TEST_CASE("Garth conjure-and-crack: the menu spends the conjured Lotus in the same plan, in both worlds")
{
    GameState s = FreshGl();
    HandGl(s, "Lightning Greaves", 7);   // {2}: nothing on the board pays it but the Lotus
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);

    bool keep_offered = false;
    for (const TurnSolver::Plan& p : plans)
    {
        const Action* g = GarthLotusGl(p);
        if (g && !TurnSolver::IsGarthLotusCrack(*g)) { keep_offered = true; }
        // The Greaves is castable only off the crack.
        if (CastsGl(p, "Lightning Greaves")) { CHECK(g != nullptr); if (g) { CHECK(TurnSolver::IsGarthLotusCrack(*g)); } }
    }
    CHECK(keep_offered);   // conjure-and-keep is still a choice

    const TurnSolver::Plan* crack = CrackPlanCasting(plans, "Lightning Greaves");
    REQUIRE(crack != nullptr);
    CHECK(GarthLotusGl(*crack)->ritual_float == 3);

    GameState r = s;
    TurnSolver::ApplyPlan(r, *crack, /*is_pre_combat=*/true);
    CHECK(CountGl(r, "Lightning Greaves") == 1);
    CHECK(CountGl(r, "Black Lotus") == 0);

    const GameState e = RunExecutorGl(s, *crack);
    CHECK(CountGl(e, "Lightning Greaves") == 1);
    CHECK(CountGl(e, "Black Lotus") == 0);
}

TEST_CASE("Garth conjure-and-crack: the colour is searched, and credited exactly")
{
    GameState s = FreshGl();
    HandGl(s, "Birds of Paradise", 8);    // {G}: only a green crack pays it
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* crack = CrackPlanCasting(plans, "Birds of Paradise");
    REQUIRE(crack != nullptr);
    CHECK(GarthLotusGl(*crack)->chosen_float_color.str() == "G");
    // A crack in any other colour cannot pay the {G}, and is rejected at the gate (CreditFixedColorSac).
    for (const TurnSolver::Plan& p : plans)
    {
        const Action* g = GarthLotusGl(p);
        if (g && TurnSolver::IsGarthLotusCrack(*g) && CastsGl(p, "Birds of Paradise"))
        { CHECK(g->chosen_float_color.str() == "G"); }
    }
    CHECK(TurnSolver::GarthLotusCrackColor(s, crack->actions, *GarthLotusGl(*crack)) == "G");

    GameState r = s;
    TurnSolver::ApplyPlan(r, *crack, /*is_pre_combat=*/true);
    CHECK(CountGl(r, "Birds of Paradise") == 1);
    CHECK(r.floating_mana.green == 2);   // GGG less the Birds' {G}
}
