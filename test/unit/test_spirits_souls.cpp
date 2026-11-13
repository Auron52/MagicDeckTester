// Spirits onboarding, integrator B (2026-11-13): Lingering Souls and the two flashback search holes it
// exposed.
//   * The card: two 1/1 white flying Spirit tokens (a Spirit lord pumps them), flashback {1}{B}.
//   * HOLE A (soundness): a flashback card in the graveyard is a castable card, so the deck stamp
//     (GyR_RetraceNames), the sim key and the dominance projection must see it -- they saw only retrace.
//   * HOLE B (completeness): B1 "cast it from hand, then flash it back in the SAME main" and B2 "flash back
//     two copies in one main" were unfindable at any budget (one graveyard cast per NAME present at plan
//     start). CollectActions now offers one flashback per graveyard copy plus a PENDING one per hand copy
//     it casts, and SubsetHasNonCanonicalGyCopies keeps a canonical prefix of the identical copies.
// Both worlds are driven on the same plans -- the rollout (TurnSolver::ApplyPlan) and the executor
// (AIEngine::TakeTurn through the committed-plan seam) -- and must reach the same board.
// test/scenarios/spirits_souls_*.json drive the whole search end to end at --budget-ms 0.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/Dominance.h"
#include "ai/DecisionProviders.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "mtg_test_seam.h"
#include "runner/GoldFishRunner.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
const char* const kSouls = "Lingering Souls";

const CardDefinition& LsDef(const std::string& name)
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

// A graveyard / hand card as real play holds it: a NAME-ONLY placeholder (DeckLoader::MakePlaceholder).
Card LsZone(const std::string& name, int number)
{
    LsDef(name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

void LsPut(GameState& s, const std::string& name, int number, bool sick = false)
{
    Permanent p;
    p.card = LsDef(name).card;
    p.card.m_number = number;
    p.controller_index = p.owner_index = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
}

// Supreme Phantom (#1, not sick) and the given lands. Land drop spent: no land-fold plan variants.
GameState LsBoard(const std::vector<std::string>& lands)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::PreCombatMain;
    s.step                = Step::MainPhase;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(LsZone("Plains", 900 + k)); }
    LsPut(s, "Supreme Phantom", 1);
    int n = 100;
    for (const std::string& l : lands) { LsPut(s, l, ++n); }
    return s;
}

std::vector<const Action*> LsGyCasts(const std::vector<Action>& acts)
{
    std::vector<const Action*> out;
    for (const Action& a : acts) { if (a.kind == Action::Kind::CastFromGraveyard) { out.push_back(&a); } }
    return out;
}

int LsCount(const std::vector<Action>& acts, Action::Kind k, const std::string& name)
{
    int n = 0;
    for (const Action& a : acts) { if (a.kind == k && a.card_name.str() == name) { ++n; } }
    return n;
}

int LsSpiritTokens(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield)
    { if (p.controller_index == 0 && p.is_token && CardHasSubtype(p.card, "Spirit")) { ++n; } }
    return n;
}

int LsZoneCount(const std::vector<Card>& z, const std::string& name)
{
    int n = 0;
    for (const Card& c : z) { if (c.m_name.str() == name) { ++n; } }
    return n;
}

void LsResolveAll(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

GameState LsRunExecutor(const GameState& s, const TurnSolver::Plan& plan)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, plan, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, LsResolveAll);
    return exec;
}

// The enumerated main-phase plan holding exactly `hand` hand casts and `gy` graveyard casts of Souls.
const TurnSolver::Plan* LsFindPlan(const std::vector<TurnSolver::Plan>& plans, int hand, int gy)
{
    for (const TurnSolver::Plan& p : plans)
    {
        if (LsCount(p.actions, Action::Kind::CastFromHand, kSouls) == hand
            && LsCount(p.actions, Action::Kind::CastFromGraveyard, kSouls) == gy) { return &p; }
    }
    return nullptr;
}

int LsTappedLands(const GameState& g)
{
    int n = 0;
    for (const Permanent& p : g.battlefield) { if (p.controller_index == 0 && p.card.IsLand() && p.tapped) { ++n; } }
    return n;
}
}   // namespace

// ---- the card --------------------------------------------------------------------------------------

TEST_CASE("Lingering Souls: two 1/1 white flying Spirit tokens; Supreme Phantom makes them 2/2")
{
    const CardDefinition& d = LsDef(kSouls);
    CHECK(d.card.IsSorcery());
    CHECK(d.card.m_mana_cost.ManaValue() == 3);
    REQUIRE(d.params.flashback_cost.has_value());
    CHECK(d.params.flashback_cost->ToString() == "{1}{B}");

    GameState s = LsBoard({});
    ApplyCastCreatesTokens(s, 0, d);
    REQUIRE(LsSpiritTokens(s) == 2);
    for (const Permanent& p : s.battlefield)
    {
        if (!p.is_token) { continue; }
        CHECK(p.card.m_power.value_or(0) == 1);
        CHECK(p.card.m_toughness.value_or(0) == 1);
        CHECK(p.card.HasKeyword(Keyword::Flying));
        CHECK(CardHasColorNamed(p.card, "W"));
        // The lord reads the token's own Spirit subtype.
        CHECK(ComputeLordBonus(p.card, s, 0, p.AnimatedAllTypes(), &p) == std::pair<int, int>{1, 1});
    }
}

TEST_CASE("Lingering Souls: the graveyard cast is priced at {1}{B} by the shared GraveyardCastSpellCost")
{
    GameState s = LsBoard({});
    const ManaCost c = GraveyardCastSpellCost(LsDef(kSouls), s);
    CHECK(c.generic == 1);
    CHECK(c.black == 1);
    CHECK(c.white == 0);
    // A non-flashback card is priced exactly as EffectiveSpellCost prices it.
    const ManaCost p = GraveyardCastSpellCost(LsDef("Supreme Phantom"), s);
    CHECK(p.ToString() == EffectiveSpellCost(LsDef("Supreme Phantom"), s).ToString());
}

// ---- HOLE A: the keys see a flashback card in the graveyard -----------------------------------------

TEST_CASE("Flashback HOLE A: the deck stamp marks a flashback card as castable from the graveyard")
{
    Decklist with_card;
    with_card.mainboard.push_back(LsDef(kSouls).card);
    with_card.mainboard.push_back(LsDef("Plains").card);
    CHECK((GoldFishRunner::DeckGraveyardReaders(with_card) & GyR_RetraceNames) != 0);
    Decklist without;
    without.mainboard.push_back(LsDef("Supreme Phantom").card);
    without.mainboard.push_back(LsDef("Plains").card);
    CHECK((GoldFishRunner::DeckGraveyardReaders(without) & GyR_RetraceNames) == 0);
}

TEST_CASE("Flashback HOLE A: the dominance projection names a flashback card in the graveyard")
{
    auto snap = [](const std::vector<std::string>& gy, std::uint32_t readers)
    {
        GameState s = LsBoard({});
        s.step = Step::Untap;
        s.deck_gy_readers = readers;
        int n = 30;
        for (const std::string& c : gy) { s.players[0].graveyard.push_back(LsZone(c, ++n)); }
        return dominance::Build(s, DefaultProvider());
    };
    // "Souls still in the graveyard" vs "Souls already flashed back (an irrelevant card in its place)".
    const dominance::DomSnap a = snap({ kSouls }, GyR_RetraceNames);
    const dominance::DomSnap b = snap({ "Supreme Phantom" }, GyR_RetraceNames);
    REQUIRE(a.comparable);
    REQUIRE(b.comparable);
    CHECK(a.exact != b.exact);
    // ...and one copy vs two.
    CHECK(snap({ kSouls }, GyR_RetraceNames).exact != snap({ kSouls, kSouls }, GyR_RetraceNames).exact);
}

// ---- HOLE B2: two copies in the graveyard, both flashed back in one main ----------------------------

TEST_CASE("Flashback HOLE B2: one candidate per graveyard COPY; a plan flashes back both; both worlds agree")
{
    GameState s = LsBoard({ "Godless Shrine", "Watery Grave", "Plains", "Plains" });
    s.players[0].graveyard.push_back(LsZone(kSouls, 40));
    s.players[0].graveyard.push_back(LsZone(kSouls, 41));
    const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, true);
    CHECK(LsGyCasts(acts).size() == 2);

    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* two = LsFindPlan(plans, 0, 2);
    REQUIRE(two != nullptr);
    // The identical copies are a canonical prefix: exactly one plan per count.
    int ones = 0, twos = 0;
    for (const TurnSolver::Plan& p : plans)
    {
        const int g = LsCount(p.actions, Action::Kind::CastFromGraveyard, kSouls);
        if (g == 1 && p.actions.size() == 1) { ++ones; }
        if (g == 2 && p.actions.size() == 2) { ++twos; }
    }
    CHECK(ones == 1);
    CHECK(twos == 1);

    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *two, true);
    const GameState exec = LsRunExecutor(s, *two);
    for (const GameState* g : { static_cast<const GameState*>(&roll), &exec })
    {
        INFO("world: " << std::string(g == &roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"));
        CHECK(LsSpiritTokens(*g) == 4);
        CHECK(LsZoneCount(g->players[0].graveyard, kSouls) == 0);   // both exiled, not back in the graveyard
        CHECK(LsTappedLands(*g) == 4);                                // {1}{B} + {1}{B}
    }
}

// ---- HOLE B1: cast from hand, then flash back, in one main ------------------------------------------

TEST_CASE("Flashback HOLE B1: a PENDING flashback beside the hand cast; the plan holds both; both worlds agree")
{
    GameState s = LsBoard({ "Godless Shrine", "Watery Grave", "Plains", "Plains", "Plains" });
    s.players[0].hand.push_back(LsZone(kSouls, 40));
    const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, true);
    CHECK(LsCount(acts, Action::Kind::CastFromHand, kSouls) >= 1);
    CHECK(LsGyCasts(acts).size() == 1);   // the pending candidate (the graveyard is empty)

    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* both = LsFindPlan(plans, 1, 1);
    REQUIRE(both != nullptr);
    // A pending flashback is never offered without its hand cast (the graveyard holds no copy).
    for (const TurnSolver::Plan& p : plans)
    {
        CHECK(LsCount(p.actions, Action::Kind::CastFromGraveyard, kSouls)
              <= LsCount(p.actions, Action::Kind::CastFromHand, kSouls));
    }

    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *both, true);
    const GameState exec = LsRunExecutor(s, *both);
    for (const GameState* g : { static_cast<const GameState*>(&roll), &exec })
    {
        INFO("world: " << std::string(g == &roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"));
        CHECK(LsSpiritTokens(*g) == 4);
        CHECK(LsZoneCount(g->players[0].hand, kSouls) == 0);
        CHECK(LsZoneCount(g->players[0].graveyard, kSouls) == 0);   // flashed back -> exiled
        CHECK(LsTappedLands(*g) == 5);                                // {2}{W} + {1}{B}
        CHECK(g->spells_cast_this_turn == s.spells_cast_this_turn + 2);
    }
}

TEST_CASE("Flashback HOLE B1: one in the graveyard and one in hand -- up to three Souls casts in one main")
{
    GameState s = LsBoard({ "Godless Shrine", "Watery Grave", "Plains", "Plains", "Plains", "Plains", "Plains" });
    s.players[0].hand.push_back(LsZone(kSouls, 40));
    s.players[0].graveyard.push_back(LsZone(kSouls, 41));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    CHECK(LsFindPlan(plans, 0, 1) != nullptr);   // the graveyard copy alone
    CHECK(LsFindPlan(plans, 1, 2) != nullptr);   // hand cast + both flashbacks (3 + 2 + 2 = 7 mana)
    CHECK(LsFindPlan(plans, 0, 2) == nullptr);   // two flashbacks need the hand cast first
}

TEST_CASE("Flashback: no flashback card anywhere -> no graveyard-cast candidate (every other deck is inert)")
{
    GameState s = LsBoard({ "Plains", "Plains", "Plains" });
    s.players[0].hand.push_back(LsZone("Supreme Phantom", 40));
    s.players[0].graveyard.push_back(LsZone("Supreme Phantom", 41));
    CHECK(LsGyCasts(TurnSolver::CollectActionsForTest(s, true)).empty());
}
