// Enchanted Evening lock cluster, STEP 2 (2026-11-26; docs/design/enchanted-evening-lock-design.md §4.2 / §6): the
// PRE-FLOAT AT THE LOCK. When the second of Enchanted Evening / Opalescence enters, every untapped land we control that
// the land-death static is about to kill is tapped for mana first (PrefloatLandsAtTypeStaticsLock, the top of the
// shared enter cascade, both worlds); the mana floats to the end of the phase. A dual floats as `wild` only under the
// deck stamp deck_float_wild_exact.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/LandPlay.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameEngine.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "runner/GoldFishRunner.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

namespace
{
const char* const kEE   = "Enchanted Evening";
const char* const kOpal = "Opalescence";
const char* const kExpl = "Exploration";

const CardDefinition& L2Def(const std::string& name)
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

Card L2Card(const std::string& name, int number)
{
    Card c = L2Def(name).card;
    c.m_number = number;
    return c;
}

int L2Put(GameState& s, const std::string& name, int number, int controller = 0, bool tapped = false)
{
    Permanent p;
    p.card = L2Card(name, number);
    p.controller_index = p.owner_index = controller;
    p.tapped = tapped;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

GameState L2Board()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::PreCombatMain;
    s.step                = Step::MainPhase;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < 8; ++k) { s.players[0].library.push_back(L2Card("Plains", 900 + k)); }
    for (int k = 0; k < 8; ++k) { s.players[1].library.push_back(L2Card("Plains", 100900 + k)); }
    return s;
}

const Permanent* L2Find(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

int L2Lands(const GameState& s, int controller)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.controller_index == controller && p.card.IsLand()) { ++n; } }
    return n;
}

int L2UntappedLands(const GameState& s, int controller)
{
    int n = 0;
    for (const Permanent& p : s.battlefield)
    { if (p.controller_index == controller && p.card.IsLand() && !p.tapped) { ++n; } }
    return n;
}

// The plan whose actions are exactly these hand casts (in any order), with no pinned target.
// Returned BY VALUE: callers pass the enumeration as a temporary.
std::optional<TurnSolver::Plan> L2FindCasts(const std::vector<TurnSolver::Plan>& plans,
                                            std::initializer_list<const char*> names)
{
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() != names.size()) { continue; }
        std::vector<std::string> want(names.begin(), names.end());
        bool ok = true;
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::CastFromHand || !a.tutor_target.str().empty()) { ok = false; break; }
            auto it = std::find(want.begin(), want.end(), a.card_name.str());
            if (it == want.end()) { ok = false; break; }
            want.erase(it);
        }
        if (ok && want.empty()) { return p; }
    }
    return std::nullopt;
}

// Both worlds: the rollout's ApplyPlan and the executor's AIEngine::TakeTurn through the real GameEngine::ResolveStack.
struct L2Worlds
{
    AIEngine   eng{ MulliganProfile::DefaultProfile(), 3, 20 };
    GameEngine ge{ eng };
    GameState  roll;
    GameState  exec;
    void Run(const GameState& s, const TurnSolver::Plan& plan)
    {
        roll = s;
        TurnSolver::ApplyPlan(roll, plan, true);
        MtgTestSeam::CommitPhasePlan(eng, plan, true);
        exec = s;
        eng.TakeTurn(exec, true, [this](GameState& st) { MtgTestSeam::GeResolveStack(ge, st); });
    }
};
}   // namespace

TEST_CASE("Lock pre-float: Opalescence completes the lock with lands untapped; the float pays a follow-up cast (both worlds)")
{
    // Enchanted Evening out. The four Plains are already tapped into the float (WWWW, Opalescence's whole cost), the
    // two Forests are untapped: the lock would kill them untapped. With the pre-float they float {G}{G} instead, and a
    // card decided AFTER the lock (a mid-phase re-solve: Enchantress's Presence's draw, a breakpoint) can be cast.
    GameState s = L2Board();
    L2Put(s, kEE, 51);
    for (int k = 0; k < 4; ++k) { L2Put(s, "Plains", 110 + k, 0, /*tapped=*/true); }
    s.floating_mana.white = 4;
    L2Put(s, "Forest", 101);
    L2Put(s, "Forest", 102);
    s.players[0].hand.push_back(L2Card(kOpal, 30));
    s.players[0].hand.push_back(L2Card(kExpl, 31));
    const std::optional<TurnSolver::Plan> cast = L2FindCasts(TurnSolver::EnumerateMainPlans(s, true), { kOpal });
    REQUIRE(cast.has_value());
    L2Worlds w;
    w.Run(s, *cast);
    for (GameState* g : { &w.roll, &w.exec })
    {
        INFO("world: " << std::string(g == &w.roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"));
        CHECK(L2Find(*g, 30) != nullptr);                                  // Opalescence resolved
        CHECK(L2Lands(*g, 0) == 0);                                        // every land died to the lock
        CHECK(g->floating_mana.green == 2);                                // ...the two Forests floated first
        CHECK(g->floating_mana.white == 0);                                // Opalescence spent the WWWW, not more
        CHECK(g->floating_mana.Total() == 2);
        // The follow-up, decided after the lock, is offered and paid from the float.
        const std::optional<TurnSolver::Plan> expl = L2FindCasts(TurnSolver::EnumerateMainPlans(*g, true), { kExpl });
        REQUIRE(expl.has_value());
        if (g == &w.roll) { TurnSolver::ApplyPlan(w.roll, *expl, true); }
        else
        {
            MtgTestSeam::CommitPhasePlan(w.eng, *expl, true);
            w.eng.TakeTurn(w.exec, true, [&w](GameState& st) { MtgTestSeam::GeResolveStack(w.ge, st); });
        }
        const Permanent* e = L2Find(*g, 31);
        REQUIRE(e != nullptr);                                             // Exploration resolved off the float
        CHECK(e->card.IsCreature());                                       // (a 1/1 under EE + Opalescence)
        CHECK(g->floating_mana.Total() == 1);
        CHECK(g->floating_mana.green == 1);
    }
    // Control: without the pre-float (no hook) the Forests die untapped and nothing floats -- the change is what pays.
    auto* hook = g_lock_prefloat_hook;
    g_lock_prefloat_hook = nullptr;
    GameState c = s;
    TurnSolver::ApplyPlan(c, *cast, true);
    g_lock_prefloat_hook = hook;
    CHECK(L2Lands(c, 0) == 0);
    CHECK(c.floating_mana.Total() == 0);
    CHECK(L2FindCasts(TurnSolver::EnumerateMainPlans(c, true), { kExpl }).has_value() == false);
}

TEST_CASE("Lock pre-float: a whole-turn plan {Opalescence, Exploration} -- the prepay's taps are neither lost nor counted twice")
{
    // Nothing tapped: the plan's joint prepay (BatchPrepayMainCasts) may tap for both casts before Opalescence resolves;
    // the pre-float then floats only what is still untapped. Six lands, five mana spent: exactly one unit is left.
    GameState s = L2Board();
    L2Put(s, kEE, 51);
    for (int k = 0; k < 4; ++k) { L2Put(s, "Plains", 110 + k); }
    L2Put(s, "Forest", 101);
    L2Put(s, "Forest", 102);
    s.players[0].hand.push_back(L2Card(kOpal, 30));
    s.players[0].hand.push_back(L2Card(kExpl, 31));
    const std::optional<TurnSolver::Plan> plan = L2FindCasts(TurnSolver::EnumerateMainPlans(s, true), { kOpal, kExpl });
    REQUIRE(plan.has_value());
    L2Worlds w;
    w.Run(s, *plan);
    for (const GameState* g : { &w.roll, &w.exec })
    {
        INFO("world: " << std::string(g == &w.roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"));
        CHECK(L2Find(*g, 30) != nullptr);
        CHECK(L2Find(*g, 31) != nullptr);
        CHECK(L2Lands(*g, 0) == 0);
        CHECK(g->floating_mana.Total() == 1);
    }
}

TEST_CASE("Lock pre-float: some lands already tapped (spent earlier) -- only the untapped rest floats; it empties at combat")
{
    // Seven Plains, two tapped earlier this phase with nothing floating. Opalescence takes four; one is left untapped
    // and floats {W}. CR 500.4: the pool empties when the pre-combat main ends, in both worlds (CrossManaBoundary at
    // GameEngine::CombatPhase / the rollout's combat).
    GameState s = L2Board();
    L2Put(s, kEE, 51);
    for (int k = 0; k < 2; ++k) { L2Put(s, "Plains", 110 + k, 0, /*tapped=*/true); }
    for (int k = 2; k < 7; ++k) { L2Put(s, "Plains", 110 + k); }
    s.players[0].hand.push_back(L2Card(kOpal, 30));
    const std::optional<TurnSolver::Plan> cast = L2FindCasts(TurnSolver::EnumerateMainPlans(s, true), { kOpal });
    REQUIRE(cast.has_value());
    L2Worlds w;
    w.Run(s, *cast);
    for (const GameState* g : { &w.roll, &w.exec })
    {
        INFO("world: " << std::string(g == &w.roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"));
        CHECK(L2Lands(*g, 0) == 0);
        CHECK(g->floating_mana.white == 1);
        CHECK(g->floating_mana.Total() == 1);
    }
    RolloutSimulateCombat(w.roll);
    MtgTestSeam::GeCombatPhase(w.ge, w.exec);
    CHECK(w.roll.floating_mana.Total() == 0);
    CHECK(w.exec.floating_mana.Total() == 0);
}

TEST_CASE("Lock pre-float: no change when only one of Enchanted Evening / Opalescence is out")
{
    // (a) Opalescence with no Enchanted Evening: lands are not enchantments, nothing dies, nothing floats.
    {
        GameState s = L2Board();
        for (int k = 0; k < 6; ++k) { L2Put(s, "Plains", 110 + k); }
        s.players[0].hand.push_back(L2Card(kOpal, 30));
        const std::optional<TurnSolver::Plan> cast = L2FindCasts(TurnSolver::EnumerateMainPlans(s, true), { kOpal });
        REQUIRE(cast.has_value());
        L2Worlds w;
        w.Run(s, *cast);
        for (const GameState* g : { &w.roll, &w.exec })
        {
            INFO("world: " << std::string(g == &w.roll ? "rollout" : "executor"));
            CHECK(L2Lands(*g, 0) == 6);
            CHECK(L2UntappedLands(*g, 0) == 2);
            CHECK(g->floating_mana.Total() == 0);
        }
    }
    // (b) Enchanted Evening with no Opalescence: every land is an enchantment, none is a creature, nothing floats.
    {
        GameState s = L2Board();
        for (int k = 0; k < 7; ++k) { L2Put(s, "Plains", 110 + k); }
        s.players[0].hand.push_back(L2Card(kEE, 30));
        const std::optional<TurnSolver::Plan> cast = L2FindCasts(TurnSolver::EnumerateMainPlans(s, true), { kEE });
        REQUIRE(cast.has_value());
        L2Worlds w;
        w.Run(s, *cast);
        for (const GameState* g : { &w.roll, &w.exec })
        {
            INFO("world: " << std::string(g == &w.roll ? "rollout" : "executor"));
            CHECK(L2Lands(*g, 0) == 7);
            CHECK(L2UntappedLands(*g, 0) == 2);
            CHECK(g->floating_mana.Total() == 0);
        }
    }
    // (c) The lock already complete: a land drop dies as it enters and is NOT floated (no priority before the SBA).
    {
        GameState s = L2Board();
        s.players[0].lands_played_this_turn = 0;
        L2Put(s, kEE, 51);
        L2Put(s, kOpal, 54);
        RefreshTypeStatics(s);
        s.players[0].hand.push_back(L2Card("Forest", 40));
        LandPlayOptions o;
        REQUIRE(PlayLandFromHand(s, 0, L2Def("Forest"), o));
        CHECK(L2Lands(s, 0) == 0);
        CHECK(s.floating_mana.Total() == 0);
    }
}

TEST_CASE("Lock pre-float: a dual floats as wild only under deck_float_wild_exact; the stamp reads the list's pips")
{
    for (const bool exact : { true, false })
    {
        CAPTURE(exact);
        GameState s = L2Board();
        s.deck_float_wild_exact = exact;
        L2Put(s, kEE, 51);
        for (int k = 0; k < 4; ++k) { L2Put(s, "Plains", 110 + k, 0, /*tapped=*/true); }
        s.floating_mana.white = 4;
        L2Put(s, "Temple Garden", 120);
        L2Put(s, "Forest", 101);
        s.players[0].hand.push_back(L2Card(kOpal, 30));
        const std::optional<TurnSolver::Plan> cast = L2FindCasts(TurnSolver::EnumerateMainPlans(s, true), { kOpal });
        REQUIRE(cast.has_value());
        L2Worlds w;
        w.Run(s, *cast);
        for (const GameState* g : { &w.roll, &w.exec })
        {
            INFO("world: " << std::string(g == &w.roll ? "rollout" : "executor"));
            CHECK(L2Lands(*g, 0) == 0);
            CHECK(g->floating_mana.green == 1);                              // the Forest, either way
            CHECK(g->floating_mana.wild == (exact ? 1 : 0));                 // the Temple Garden: wild, or not at all
            CHECK(g->floating_mana.white == 0);
            CHECK(g->floating_mana.Total() == (exact ? 2 : 1));
        }
    }
    // The stamp: the Enchanted Evening list's pips are all G / W (EE's {W/U} by its W half) -> exact; a list with a
    // {U} pip beside a G/W dual is not.
    auto stamp = [](std::initializer_list<const char*> names) {
        Decklist d;
        int n = 1;
        for (const char* nm : names) { d.mainboard.push_back(L2Card(nm, n++)); }
        GameState g;
        GoldFishRunner::StampDeckTraits(g, d);
        return g.deck_float_wild_exact;
    };
    CHECK(stamp({ kEE, kOpal, "Temple Garden", "Sunpetal Grove", "Forest", "Plains", "Sterling Grove" }));
    CHECK_FALSE(stamp({ kEE, "Temple Garden", "Brainstorm" }));
    CHECK(stamp({ "Forest", "Brainstorm" }));                                // no dual: nothing to decide
}
