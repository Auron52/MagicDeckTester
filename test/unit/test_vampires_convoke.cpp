// Unit tests for the Vampires claude-play sweep's CONVOKE fixes (2026-10-18; ledger "Sweep fixes"):
//   A. phantom mana -- two Markov Barons counting the same bodies; a reduction above the bodies tapped;
//   B. a creature the SAME plan casts first is a (free, summoning-sick) convoke body;
//   C. the arm set ranks bodies by TRUE power (lords included) and covers the pip colour;
//   K. the whole-turn batch prepay bills a convoked cast at its reduced cost.
//
// WHY THESE ARE UNIT TESTS. Every one of these defects is silent: the phantom-mana line resolves with
// lands left untapped, the missing same-plan arm is simply never enumerated, the mis-ranked arm only
// changes which attacker taps, and the prepay's over-tap leaves floating mana that empties at the step's
// end. The invariant they share is MANA CONSERVATION on a board of one-mana sources: what the casts cost
// must equal what was tapped (lands + convoke bodies) minus what is left floating.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameEngine.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(
            ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
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

GameState Fresh()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;   // land drop spent
    s.m_provider = &SelectDecisionProvider(DeckLoader::LoadFromFile(
        ResolveHeuristicDefaultsPath("decks/Vampires/Vampire.cod")));
    for (int k = 0; k < 20; ++k)
    {
        Card c = Def("Swamp").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

int Put(GameState& s, const std::string& name, int number, bool sick = false, int ctrl = 0)
{
    Permanent p;
    p.card              = Def(name).card;
    p.card.m_number     = number;
    p.controller_index  = ctrl;
    p.owner_index       = ctrl;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return number;
}

int Hand(GameState& s, const std::string& name, int number)
{
    Card c = Def(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
    return number;
}

const Permanent* FindNum(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

void ResolveAll(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

int CountCast(const TurnSolver::Plan& p, const std::string& name)
{
    int n = 0;
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { ++n; } }
    return n;
}

// Mana conservation on a board of one-mana sources: the printed MV of every card that LEFT THE HAND onto
// the battlefield == lands newly tapped + creatures newly tapped (convoke) - floating mana left over.
struct Books { int cast_mv = 0, lands = 0, bodies = 0, floating = 0; };
Books Account(const GameState& before, const GameState& after)
{
    Books b;
    for (const Card& hc : before.players[0].hand)
    {
        if (FindNum(after, hc.m_number) == nullptr) { continue; }
        b.cast_mv += Def(hc.m_name.str()).card.m_mana_cost.ManaValue();
    }
    for (const Permanent& p : after.battlefield)
    {
        if (p.controller_index != 0 || !p.tapped) { continue; }
        const Permanent* q = FindNum(before, p.card.m_number);
        if (q != nullptr && q->tapped) { continue; }   // already tapped before the plan
        // A NEW tapped creature is a same-plan arrival that convoked (item B) -- a body like any other.
        (p.card.IsLand() ? b.lands : b.bodies) += 1;
    }
    b.floating = after.floating_mana.Total();
    return b;
}

GameState ApplyRollout(const GameState& s, const TurnSolver::Plan& p, bool pre)
{
    GameState r = s;
    TurnSolver::ApplyPlan(r, p, pre);
    return r;
}

GameState ApplyExecutor(const GameState& s, const TurnSolver::Plan& p, bool pre)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    eng.SetSearchPostCombat(true);
    MtgTestSeam::CommitPhasePlan(eng, p, pre);
    GameState e = s;
    eng.TakeTurn(e, pre, ResolveAll);
    return e;
}

TurnSolver::Plan BaseLine(const TurnSolver::Plan& p)
{
    TurnSolver::Plan line = p;
    if (line.bp_choice < 0) { line.bp_choice = TurnSolver::kBpEmptyChoice; line.bp_all = true; }
    return line;
}

}   // namespace

// ---- A. two Barons cannot count the same bodies -----------------------------------------------------

TEST_CASE("Convoke A: every two-Baron plan conserves mana in both worlds (no body counted twice)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Vampire of the Dire Moon", 1, /*sick=*/true);   // black (pip) body
    Put(s, "Stromkirk Condemned", 2, /*sick=*/true);        // black body
    Put(s, "Falkenrath Gorger", 3, /*sick=*/true);          // red body: pays {1} only
    Put(s, "Swamp", 50); Put(s, "Swamp", 51); Put(s, "Swamp", 52);
    Hand(s, "Markov Baron", 10);
    Hand(s, "Markov Baron", 11);
    // Six resources (three bodies + three Swamps) for six mana: both Barons ARE castable -- one convoked
    // by the three bodies, the other off the lands -- but never both convoked by the same three.
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    int both = 0;
    for (const TurnSolver::Plan& p0 : plans)
    {
        if (CountCast(p0, "Markov Baron") != 2) { continue; }
        int counted = 0;
        for (const Action& a : p0.actions) { counted += a.convoke_green + a.convoke_other; }
        INFO("plan bodies counted = " << counted);
        CHECK(counted <= 4);   // three board bodies + the first Baron (sick) for the second
        const TurnSolver::Plan line = BaseLine(p0);
        for (int world = 0; world < 2; ++world)
        {
            const GameState r = world == 0 ? ApplyRollout(s, line, true) : ApplyExecutor(s, line, true);
            const Books b = Account(s, r);
            INFO("world=" << world << " cast_mv=" << b.cast_mv << " lands=" << b.lands << " bodies=" << b.bodies
                 << " floating=" << b.floating);
            CHECK(b.cast_mv == b.lands + b.bodies - b.floating);
            if (FindNum(r, 10) != nullptr && FindNum(r, 11) != nullptr) { ++both; }
        }
    }
    CHECK(both > 0);   // the legal two-Baron line is still there
}

TEST_CASE("Convoke A: a reduction can never exceed the bodies actually tapped (cast-time taps)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Vampire of the Dire Moon", 1, /*sick=*/true);
    Put(s, "Swamp", 50);
    Hand(s, "Markov Baron", 10);
    // A stale arm that counts three bodies when one exists: the cast taps one and is reduced by one.
    ManaCost cost = Def("Markov Baron").card.m_mana_cost;   // {2}{B}
    {
        ConvokeCastTaps cv;
        cv.Tap(s, 0, Def("Markov Baron"), 3, 0, cost);
        CHECK(cv.pip == 1);
        CHECK(cost.ManaValue() == 2);                      // {2}{B} - one black body = {2}
        CHECK(FindNum(s, 1)->tapped);
    }   // not committed: the destructor untaps the body (a failed payment leaves the board as it was)
    CHECK_FALSE(FindNum(s, 1)->tapped);
    // Chord of Calling's convoke keeps its pre-pass (not a creature spell).
    CHECK_FALSE(ConvokeTapsAtCast(&Def("Chord of Calling")));
    CHECK(ConvokeTapsAtCast(&Def("Markov Baron")));
}

// ---- B. a creature cast earlier in the plan convokes -----------------------------------------------

TEST_CASE("Convoke B: cast Vampire of the Dire Moon, then Baron convoked by it -- one plan, both worlds")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Swamp", 50); Put(s, "Swamp", 51); Put(s, "Swamp", 52);
    Hand(s, "Vampire of the Dire Moon", 10);   // {B}
    Hand(s, "Markov Baron", 11);               // {2}{B}: {2} once the Dire Moon (black, sick) convokes
    // Three Swamps, four mana of spells: castable only with the same-plan body.
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    int found = 0;
    for (const TurnSolver::Plan& p0 : plans)
    {
        if (CountCast(p0, "Markov Baron") != 1 || CountCast(p0, "Vampire of the Dire Moon") != 1) { continue; }
        bool plan_arm = false;
        for (const Action& a : p0.actions) { if (a.convoke_plan_pip > 0) { plan_arm = true; } }
        CHECK(plan_arm);
        const TurnSolver::Plan line = BaseLine(p0);
        for (int world = 0; world < 2; ++world)
        {
            const GameState r = world == 0 ? ApplyRollout(s, line, true) : ApplyExecutor(s, line, true);
            INFO("world=" << world);
            REQUIRE(FindNum(r, 10) != nullptr);
            REQUIRE(FindNum(r, 11) != nullptr);
            CHECK(FindNum(r, 10)->tapped);              // the Dire Moon convoked
            const Books b = Account(s, r);
            CHECK(b.lands == 3);
            CHECK(b.cast_mv == b.lands + 1 - b.floating);   // + the sick Dire Moon's tap
        }
        ++found;
    }
    CHECK(found > 0);
}

TEST_CASE("Convoke B: a plan-body arm is cast after its arrivals; the ordering check sees a bad order")
{
    EnsureCardsLoaded();
    Action baron;
    baron.kind = Action::Kind::CastFromHand;
    baron.card_name = std::string("Markov Baron");
    baron.def = &Def("Markov Baron");
    baron.convoke_green = 1;
    baron.convoke_plan_pip = 1;
    Action moon;
    moon.kind = Action::Kind::CastFromHand;
    moon.card_name = std::string("Vampire of the Dire Moon");
    moon.def = &Def("Vampire of the Dire Moon");
    Action gorger;
    gorger.kind = Action::Kind::CastFromHand;
    gorger.card_name = std::string("Falkenrath Gorger");
    gorger.def = &Def("Falkenrath Gorger");
    const std::vector<Action> acts = { baron, gorger, moon };
    CHECK(OrderingPlacesConvokeBeforeArrivals(acts));
    std::vector<int> order = { 0, 1, 2 };
    ApplyConvokeArrivalDependencyOrder(acts, order);
    CHECK(order == std::vector<int>{ 1, 2, 0 });   // after the BLACK arrival (the red Gorger is not one)
    const std::vector<Action> ok = { moon, baron, gorger };
    CHECK_FALSE(OrderingPlacesConvokeBeforeArrivals(ok));
    std::vector<int> o2 = { 0, 1, 2 };
    ApplyConvokeArrivalDependencyOrder(ok, o2);
    CHECK(o2 == std::vector<int>{ 0, 1, 2 });       // already after it: untouched
}

// ---- C. true power and the pip colour -------------------------------------------------------------

TEST_CASE("Convoke C: arms rank bodies by lord-inclusive power and offer the pip-coloured body")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    // The Gorger (#1, red 2/1) is a 3/2 under the board Baron's lord bonus; the board Baron (#2, black
    // 2/2) is not pumped by itself. Both can attack. Tapping the Baron pays {B} and gives up 2 power;
    // tapping the Gorger pays {1} and gives up 3.
    Put(s, "Falkenrath Gorger", 1);
    Put(s, "Markov Baron", 2);
    const CardDefinition& baron = Def("Markov Baron");
    const ConvokeRules r = ConvokeRulesFor(baron, /*post_combat_or_cleanup=*/false);
    CHECK(r.true_power);
    const std::vector<ConvokeArmSpec> arms = CreatureConvokeArms(s, 0, baron.card.m_mana_cost, r);
    bool baron_only = false, gorger_only = false;
    for (const ConvokeArmSpec& a : arms)
    {
        if (a.pip == 1 && a.other == 0) { baron_only = true; CHECK(a.lost_power == 2); CHECK(a.reduced.black == 0); }
        if (a.pip == 0 && a.other == 1) { gorger_only = true; CHECK(a.lost_power == 3); CHECK(a.reduced.black == 1); }
    }
    CHECK(baron_only);    // was unreachable: the old prefix arms took the (number-ordered) Gorger first
    CHECK(gorger_only);
    CHECK(arms.front().pip + arms.front().other == 0);   // no free body: the front is the plain cast
    // The classifier's costly order is TRUE power: two black would-attack bodies, the pumped one last.
    GameState t = Fresh();
    Put(t, "Stromkirk Condemned", 5);                   // black 2/2 -> 3/3 under the Baron below
    Put(t, "Markov Baron", 6);                          // black 2/2 (not pumped by itself)
    const ConvokeClasses cc = ClassifyConvokeBodiesEx(t, 0, r);
    REQUIRE(cc.atk_green.size() == 2);
    CHECK(cc.atk_green[0] == 6);                         // the 2-power Baron before the 3-power Condemned
}

// ---- K. the batch prepay bills the reduced cost ----------------------------------------------------

TEST_CASE("Convoke K: Pit Fighter + a fully convoked Baron taps one land, floats nothing, loses no life")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Vampire of the Dire Moon", 1, /*sick=*/true);
    Put(s, "Stromkirk Condemned", 2, /*sick=*/true);
    Put(s, "Legion Lieutenant", 3, /*sick=*/true);
    Put(s, "Blackcleave Cliffs", 50);
    Put(s, "Secluded Courtyard", 51);
    Put(s, "Swamp", 52);
    Put(s, "Voldaren Estate", 53);
    Hand(s, "Falkenrath Pit Fighter", 10);   // {R}
    Hand(s, "Markov Baron", 11);             // {2}{B}: {0} with the three black bodies
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    int found = 0;
    for (const TurnSolver::Plan& p0 : plans)
    {
        if (CountCast(p0, "Markov Baron") != 1 || CountCast(p0, "Falkenrath Pit Fighter") != 1) { continue; }
        if (p0.actions.size() != 2) { continue; }
        const Action* b = nullptr;
        for (const Action& a : p0.actions) { if (a.card_name.str() == "Markov Baron") { b = &a; } }
        if (b == nullptr || b->convoke_green + b->convoke_other != 3) { continue; }
        const TurnSolver::Plan line = BaseLine(p0);
        for (int world = 0; world < 2; ++world)
        {
            const GameState r = world == 0 ? ApplyRollout(s, line, true) : ApplyExecutor(s, line, true);
            INFO("world=" << world);
            REQUIRE(FindNum(r, 10) != nullptr);
            REQUIRE(FindNum(r, 11) != nullptr);
            const Books bk = Account(s, r);
            CHECK(bk.lands == 1);
            CHECK(bk.bodies == 3);
            CHECK(bk.floating == 0);
            CHECK(r.players[0].life == s.players[0].life);
            CHECK_FALSE(FindNum(r, 53)->tapped);        // the Estate is still up for its Blood ability
        }
        ++found;
    }
    CHECK(found > 0);
}

// ---- lockstep over every Baron plan under contention -------------------------------------------------

TEST_CASE("Convoke lockstep: every Baron plan (two Barons, a same-plan body) realises identically in both worlds")
{
    EnsureCardsLoaded();
    for (bool main2 : { false, true })
    {
        GameState s = Fresh();
        s.phase = main2 ? Phase::PostCombatMain : Phase::PreCombatMain;
        Put(s, "Vampire of the Dire Moon", 1, /*sick=*/true);
        Put(s, "Falkenrath Gorger", 2);            // would attack pre-combat: costly
        Put(s, "Stromkirk Condemned", 3);          // would attack pre-combat: costly
        Put(s, "Swamp", 50); Put(s, "Swamp", 51); Put(s, "Blackcleave Cliffs", 52);
        Hand(s, "Markov Baron", 10);
        Hand(s, "Markov Baron", 11);
        Hand(s, "Vampire of the Dire Moon", 12);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/!main2);
        int checked = 0;
        for (const TurnSolver::Plan& p0 : plans)
        {
            if (CountCast(p0, "Markov Baron") == 0) { continue; }
            const TurnSolver::Plan line = BaseLine(p0);
            const GameState roll = ApplyRollout(s, line, !main2);
            const GameState exec = ApplyExecutor(s, line, !main2);
            std::string desc;
            for (const Action& a : line.actions)
            { desc += a.card_name.str() + "/C" + std::to_string(a.convoke_green) + "," + std::to_string(a.convoke_other)
                    + "+" + std::to_string(static_cast<int>(a.convoke_plan_pip)) + " "; }
            INFO("main2=" << main2 << " [" << desc << "]");
            CHECK(roll.battlefield.size() == exec.battlefield.size());
            for (const Permanent& p : roll.battlefield)
            {
                const Permanent* q = FindNum(exec, p.card.m_number);
                REQUIRE(q != nullptr);
                CHECK(q->tapped == p.tapped);
            }
            CHECK(roll.players[0].hand.size() == exec.players[0].hand.size());
            CHECK(roll.players[0].life == exec.players[0].life);
            const Books br = Account(s, roll);
            CHECK(br.cast_mv == br.lands + br.bodies - br.floating);
            ++checked;
        }
        CHECK(checked > 0);
    }
}
