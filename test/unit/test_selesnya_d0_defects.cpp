// The two SelesnyaLifegain depth-0 play defects of the 2026-10-10 discard round
// (docs/design/discard-round-play-defects-2026-10-10.md sections 1 and 2), pinned on the boards of the
// game they were found in: selesnya_regression_d0_s2002 gi 344, seed 2346.
//
// 1. PHANTOM PARAGON (MTG_COLOR_EXACT_SCALED). T4-T8 the board is Selesnya Sanctuary + Branchloft
//    Pathway (its {G} face) + Llanowar Elves + Wellwisher + an untapped Elvish Archdruid, and the d0
//    plan was "cast Nykthos Paragon" ({4}{W}{W}) five turns running. The flat pool books the Karoo's
//    fixed {G}{W} as two WILD (so {g5, wild2} "pays" it), the presence test sees a {W} source, and the
//    colour-exact gate -- the one check that knows a Karoo makes ONE {W} -- stood down for the whole
//    board because an untapped scaled dork was on it. The executor's payment (the backtracker's exact
//    bundle) correctly refused, and nothing was cast. The tests assert the solver and the payer now
//    AGREE on that board (and on its payable twin), and that the =0 lever restores the old verdict.
//
// 2. A NON-MANA {T} ABILITY IS NOT AN ATTACKER (MTG_ATTACK_TAP_ABILITY_DISCOUNT), and the mana-tap
//    discount's `need` was dead (MTG_ATTACK_TAP_NEED_EXACT): pool_noncreature keeps every mana dork, so
//    a plan whose dorks finish the payment discounted nothing. T8 of the policy arm: "cast X=4 Genesis
//    Wave + tap Wellwisher for life" was projected exactly lethal off Wellwisher's 2 power. Verdant
//    Sun's Avatar ({5}{G}{G}, all seven mana) stands in for the X spell so the cost is fixed.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/GameSetup.h"
#include "core/SpellEffects.h"
#include "core/HeuristicDefaults.h"

#include "mtg_test_seam.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& Def(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

void Put(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card              = Def(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = false;
    s.battlefield.push_back(p);
}

Card HandCard(const std::string& name, int number)
{
    Card c = Def(name).card;
    c.m_number = number;
    return c;
}

// The gi 344 board from turn 4 on, pre-combat, with `hand` in hand. `w_twin` swaps the Pathway for a
// Brushland -- a second real {W} -- which makes {W}{W} payable (the soundness twin). `salt` only keeps
// the per-thread Solve memo from serving one arm's plan to the other (it keys on the state).
GameState Board(const std::vector<std::string>& hand, int opp_life, bool w_twin = false, int salt = 0)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 8;
    s.players[0].life     = 32 + salt;
    s.players[1].life     = opp_life;
    Put(s, "Llanowar Elves", 40);
    Put(s, "Selesnya Sanctuary", 52);
    Put(s, "Wellwisher", 58);
    Put(s, w_twin ? "Brushland" : "Branchloft Pathway", 13);
    Put(s, "Elvish Archdruid", 22);
    s.players[0].lands_played_this_turn = 1;           // no land drop left to muddy the menu
    int num = 100;
    for (const std::string& n : hand) { s.players[0].hand.push_back(HandCard(n, num++)); }
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(HandCard("Forest", 900 + k)); }
    return s;
}

bool Casts(const TurnSolver::Plan& p, const std::string& name)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { return true; } }
    return false;
}

bool TapsForLife(const TurnSolver::Plan& p)
{
    for (const Action& a : p.actions)
    {
        if (a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::TapLifegain)
        { return true; }
    }
    return false;
}

Action CastOf(const std::string& name, const GameState& s)
{
    const CardDefinition& def = Def(name);
    Action a;
    a.kind           = Action::Kind::CastFromHand;
    a.cost           = EffectiveSpellCost(def, s);
    a.is_noncreature = !def.card.IsCreature();
    return a;
}

// The executor's own payment, on a copy.
bool ExecutorPays(const GameState& board, const ManaCost& cost)
{
    GameState s = board;
    AIEngine  eng;
    ManaPool  avail = AvailableManaPool(s);
    return MtgTestSeam::TapForCost(eng, s, cost, avail, /*for_creature=*/true);
}

struct ArmScope
{
    explicit ArmScope(std::initializer_list<std::pair<heurarm::Slot, int>> v)
    { for (const auto& [slot, on] : v) { heurarm::t_arm[slot] = static_cast<std::int8_t>(on); } }
    ~ArmScope() { heurarm::Clear(); }
};

}   // namespace

TEST_CASE("phantom Paragon: a Karoo's {G}{W} is not {W}{W} beside an untapped scaled dork")
{
    EnsureCardsLoaded();
    heurarm::Clear();
    const GameState s = Board({"Nykthos Paragon"}, /*opp_life=*/18);
    const ManaCost paragon = EffectiveSpellCost(Def("Nykthos Paragon"), s);
    REQUIRE(paragon.white == 2);
    REQUIRE(paragon.ManaValue() == 6);

    // The premise: the flat pool over-credits (Sanctuary = two wild) and the real payment refuses --
    // Sanctuary is the ONLY {W} source and it makes one.
    CHECK(AvailableManaPool(s).CanPay(paragon));
    CHECK_FALSE(ExecutorPays(s, paragon));
    {
        GameState t = s;
        CHECK_FALSE(TapForCostDirect(t, paragon, /*for_creature=*/true));   // the rollout twin agrees
    }

    // The fix: the colour gate runs on this board and rejects the cast.
    const std::vector<Action> cands = { CastOf("Nykthos Paragon", s) };
    const std::vector<int>    sel   = { 0 };
    const ColorFeasibility cf = BuildColorFeasibility(s);
    REQUIRE(cf.usable);
    CHECK_FALSE(cf.Payable(cands, sel, ManaPool{}));

    // ...so neither the greedy d0 / playout Solve nor the search's enumerator offers it.
    CHECK_FALSE(Casts(TurnSolver::Solve(s, true, TurnSolver::GreedyPermit(TurnSolver::GreedySite::HorizonLeaf, 0)),
                      "Nykthos Paragon"));
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    { CHECK_FALSE(Casts(p, "Nykthos Paragon")); }

    // An unbounded scaled source still never makes the gate reject GREEN: the Archdruid's yield grows
    // with every Elf the plan casts, so no {G} count may be called short.
    {
        Action g; g.kind = Action::Kind::CastFromHand; g.cost.green = 12;
        CHECK(cf.Payable({ g }, { 0 }, ManaPool{}));
    }
}

TEST_CASE("phantom Paragon: the payable twin (a second real {W}) is still admitted and paid")
{
    EnsureCardsLoaded();
    heurarm::Clear();
    const GameState s = Board({"Nykthos Paragon"}, 18, /*w_twin=*/true);   // Brushland for the Pathway
    const ManaCost paragon = EffectiveSpellCost(Def("Nykthos Paragon"), s);
    CHECK(ExecutorPays(s, paragon));
    const ColorFeasibility cf = BuildColorFeasibility(s);
    REQUIRE(cf.usable);
    CHECK(cf.Payable({ CastOf("Nykthos Paragon", s) }, { 0 }, ManaPool{}));
    bool offered = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    { if (Casts(p, "Nykthos Paragon")) { offered = true; } }
    CHECK(offered);
}

TEST_CASE("phantom Paragon: MTG_COLOR_EXACT_SCALED=0 restores the stand-down (and the phantom)")
{
    EnsureCardsLoaded();
    ArmScope arm({ { heurarm::COLOR_EXACT_SCALED, 0 } });
    const GameState s = Board({"Nykthos Paragon"}, 18, false, /*salt=*/1);
    CHECK_FALSE(BuildColorFeasibility(s).usable);
    bool offered = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    { if (Casts(p, "Nykthos Paragon")) { offered = true; } }
    CHECK(offered);   // the legacy enumerator offers the cast the payer cannot make
}

TEST_CASE("attack projection: a creature tapped for its {T} lifegain ability is not an attacker")
{
    EnsureCardsLoaded();
    heurarm::Clear();
    // Opponent at 2. Attackers: Llanowar 2, Wellwisher 2, Archdruid 2 (the Archdruid pumps the other
    // Elves). Verdant Sun's Avatar costs all seven mana: Sanctuary 2 + Pathway 1 + Llanowar 1 +
    // Archdruid 3 -- both attacking dorks tap, so only Wellwisher (2) is left to swing.
    const GameState s = Board({"Verdant Sun's Avatar"}, /*opp_life=*/2);
    bool saw_avatar_only = false, saw_avatar_and_life = false, saw_life_only = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        const bool avatar = Casts(p, "Verdant Sun's Avatar");
        const bool life   = TapsForLife(p);
        CAPTURE(avatar); CAPTURE(life);
        if (avatar && life)
        {
            // Every creature that could attack is tapped: nothing connects, so it does not win.
            saw_avatar_and_life = true;
            CHECK_FALSE(p.wins_this_turn);
        }
        else if (avatar)
        {
            saw_avatar_only = true;
            CHECK(p.wins_this_turn);          // Wellwisher swings for exactly 2
        }
        else if (life)
        {
            saw_life_only = true;
            CHECK(p.wins_this_turn);          // Llanowar + Archdruid swing for 4
        }
    }
    CHECK(saw_avatar_and_life);
    CHECK(saw_avatar_only);
    CHECK(saw_life_only);
}

TEST_CASE("attack projection: the mana-tap need counts the attacking dorks the payment spends")
{
    EnsureCardsLoaded();
    heurarm::Clear();
    // Opponent at 3: the Avatar taps Llanowar + Archdruid, Wellwisher alone hits for 2 -- not lethal.
    // The legacy need (cost - pool_noncreature, which holds both dorks) was 7 - 7 = 0, so it read 6.
    const GameState s = Board({"Verdant Sun's Avatar"}, /*opp_life=*/3, false, /*salt=*/2);
    bool saw = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        if (!Casts(p, "Verdant Sun's Avatar") || TapsForLife(p)) { continue; }
        saw = true;
        CHECK_FALSE(p.wins_this_turn);
    }
    CHECK(saw);
    // ...and the greedy Solve (the d0 runner / playout policy) projects it the same way.
    const TurnSolver::Plan g = TurnSolver::Solve(s, true, TurnSolver::GreedyPermit(TurnSolver::GreedySite::HorizonLeaf, 0));
    if (Casts(g, "Verdant Sun's Avatar")) { CHECK_FALSE(g.wins_this_turn); }
}

TEST_CASE("attack projection: both levers =0 restore the legacy (over-crediting) projection")
{
    EnsureCardsLoaded();
    ArmScope arm({ { heurarm::ATTACK_TAP_NEED_EXACT, 0 }, { heurarm::ATTACK_TAP_ABILITY_DISCOUNT, 0 } });
    const GameState s = Board({"Verdant Sun's Avatar"}, /*opp_life=*/2, false, /*salt=*/3);
    bool saw = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        if (!(Casts(p, "Verdant Sun's Avatar") && TapsForLife(p))) { continue; }
        saw = true;
        CHECK(p.wins_this_turn);   // the phantom: nothing can attack, yet it read as lethal
    }
    CHECK(saw);
}
