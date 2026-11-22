// WORLDFIRE's mass exile fires LEAVES-THE-BATTLEFIELD triggers through the engine's shared path (2026-11-22; the
// USER rejected the deferral "LTB triggers are not fired": "that seems like a future risk even if it doesn't impact
// this deck, so we should build something there"). ResolveExileAllAndSetLife hands every exiled permanent, as it
// last existed, to PermanentsLeftBattlefieldTogether, which fires each one's own LTB through the single-leave hook
// FireLeavesBattlefieldTriggers, plus Athreos, Shroud-Veiled's exile watcher with the CR 603.10a look-back, in
// APNAP order, AFTER the life clause. Real cards only; executor (EffectHandler) and search (TurnSolver apply_one)
// must reach the same state.
#include <doctest/doctest.h>
#include "ai/AIEngine.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {
const CardDefinition& LDef(const std::string& name)
{
    static const bool loaded = [] { CardDatabase::Instance().LoadFromJson(
        ResolveHeuristicDefaultsPath("src/cards/data/cards.json")); return true; }();
    REQUIRE(loaded);
    const auto* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    return *d;
}
Card LCard(const std::string& name, int id)
{ Card c = LDef(name).card; c.m_number = id; return c; }
Permanent& LPut(GameState& s, const std::string& name, int id, int who = 0)
{
    Permanent p; p.card = LCard(name, id); p.controller_index = who; p.owner_index = who;
    p.entered_this_turn = false; s.battlefield.push_back(p);
    return s.battlefield.back();
}
// Nine Mountains to pay {6}{R}{R}{R}, Worldfire in hand, a library to draw from.
GameState LBase()
{
    GameState s; s.active_player_index = 0; s.turn_number = 5;
    for (int i = 0; i < 9; ++i) { LPut(s, "Mountain", 10 + i); }
    s.players[0].hand = {LCard("Worldfire", 30)};
    for (int i = 0; i < 5; ++i) { s.players[0].library.push_back(LCard("Island", 40 + i)); }
    return s;
}
// Cast + resolve Worldfire in the EXECUTOR.
GameState RunExecutor(const GameState& base)
{
    GameState real = base;
    AIEngine ai;
    ManaPool available = AvailableManaPool(real, nullptr);
    MtgTestSeam::CastSpellFromHand(ai, real, real.players[0].hand.front(), available, 0);
    REQUIRE(real.stack.size() == 1);
    StackEntry entry = real.stack.back(); real.stack.pop_back();
    EffectHandler::Resolve(real, entry, LDef("Worldfire"));
    return real;
}
// The same cast applied by the SEARCH (apply_one).
GameState RunRollout(const GameState& base)
{
    GameState roll = base;
    TurnSolver::Plan plan; Action cast; cast.kind = Action::Kind::CastFromHand;
    cast.card_name = "Worldfire"; cast.hand_index = 0;
    plan.actions.push_back(cast);
    TurnSolver::ApplyPlan(roll, plan, true);
    return roll;
}
using BfRow = std::tuple<std::string, int, int, int, int, bool>;   // name, ctrl, owner, power, toughness, token
std::vector<BfRow> Board(const GameState& s)
{
    std::vector<BfRow> out;
    for (const Permanent& p : s.battlefield)
    {
        out.emplace_back(p.card.m_name.str(), p.controller_index, p.owner_index,
                         p.card.m_power.value_or(-99), p.card.m_toughness.value_or(-99), p.is_token);
    }
    std::sort(out.begin(), out.end());
    return out;
}
std::vector<std::pair<std::string, int>> Zone(const std::vector<Card>& z)
{
    std::vector<std::pair<std::string, int>> out;
    for (const Card& c : z) { out.emplace_back(c.m_name.str(), c.m_number); }
    std::sort(out.begin(), out.end());
    return out;
}
const Card* InExile(const GameState& s, int number)
{
    for (const Card& c : s.exile) { if (c.m_number == number) { return &c; } }
    return nullptr;
}
void CheckWorldsAgree(const GameState& real, const GameState& roll)
{
    CHECK(Board(real) == Board(roll));
    CHECK(Zone(real.exile) == Zone(roll.exile));
    CHECK(real.players[0].life == roll.players[0].life);
    CHECK(real.players[1].life == roll.players[1].life);
    CHECK(real.athreos_coin_ids == roll.athreos_coin_ids);
}
}

TEST_CASE("worldfire LTB: each exiled permanent's leave trigger fires once, after the life clause, for its controller")
{
    // OURS: a Keldon Marauders (ltb_damage_any 1) and a TOKEN Marauders -- the token ceases to exist, its LTB still
    // fires. THEIRS: a Skyclave Apparition holding OUR Keldon Halberdier (MV 5) in its linked exile; its LTB makes
    // the exiled card's OWNER (us) a 5/5 Illusion.
    GameState base = LBase();
    LPut(base, "Keldon Marauders", 22);
    {
        Permanent tok; tok.card = LCard("Keldon Marauders", base.next_token_number++);
        tok.is_token = true; tok.controller_index = 0; tok.owner_index = 0; base.battlefield.push_back(tok);
    }
    LPut(base, "Skyclave Apparition", 100010, 1).linked_exile_number = 23;
    Card held = LCard("Keldon Halberdier", 23);
    held.m_staged_expiry = kSkyclaveExileBase - (2 * 100010 + 0);
    base.exile.push_back(held);
    LPut(base, "Island", 100001, 1);

    const GameState real = RunExecutor(base);
    const GameState roll = RunRollout(base);
    for (const GameState* g : {&real, &roll})
    {
        const GameState& s = *g;
        // Life became 1 FIRST, then the two Marauders pings (one each -- never twice, never skipped for the token).
        CHECK(s.players[1].life == -1);
        CHECK(s.players[0].life == 1);
        // The only permanent left: our 5/5 blue Illusion from the opponent's Skyclave (the owner creates it).
        REQUIRE(s.battlefield.size() == 1);
        const Permanent& ill = s.battlefield[0];
        CHECK(ill.is_token);
        CHECK(ill.controller_index == 0);
        CHECK(ill.card.m_power.value_or(0) == 5);
        CHECK(ill.card.m_toughness.value_or(0) == 5);
        // The Halberdier stays exiled with its link spent; the Marauders and the Apparition are exiled cards.
        const Card* h = InExile(s, 23);
        REQUIRE(h != nullptr);
        CHECK_FALSE(CardIsSkyclaveExile(*h));
        CHECK(InExile(s, 22) != nullptr);
        CHECK(InExile(s, 100010) != nullptr);
        CHECK(s.permanent_left_under_control_this_turn);
    }
    CheckWorldsAgree(real, roll);
}

TEST_CASE("worldfire LTB: the OPPONENT's Keldon Marauders pings US -- the trigger's controller is its controller")
{
    GameState base = LBase();
    LPut(base, "Keldon Marauders", 100020, 1);
    const GameState real = RunExecutor(base);
    const GameState roll = RunRollout(base);
    for (const GameState* g : {&real, &roll})
    {
        CHECK(g->players[0].life == 0);
        CHECK(g->players[1].life == 1);
        CHECK(g->battlefield.empty());
    }
    CheckWorldsAgree(real, roll);
}

TEST_CASE("worldfire LTB: Athreos exiled ALONGSIDE a coin-marked creature still returns it (CR 603.10a look-back)")
{
    GameState base = LBase();
    LPut(base, "Athreos, Shroud-Veiled", 24);
    LPut(base, "Keldon Halberdier", 25);
    base.athreos_coin_ids.push_back(25);
    // An opponent's coin-marked spawn-band creature card too: it returns under OUR control (Athreos's controller).
    LPut(base, "Keldon Marauders", 100030, 1);
    base.athreos_coin_ids.push_back(100030);

    const GameState real = RunExecutor(base);
    const GameState roll = RunRollout(base);
    for (const GameState* g : {&real, &roll})
    {
        const GameState& s = *g;
        CHECK(s.athreos_coin_ids.empty());
        CHECK(InExile(s, 24) != nullptr);          // Athreos itself stays exiled
        REQUIRE(s.battlefield.size() == 2);
        for (const Permanent& p : s.battlefield)
        {
            CHECK(p.controller_index == 0);
            CHECK((p.card.m_number == 25 || p.card.m_number == 100030));
            CHECK_FALSE(p.is_token);
        }
        // The returned Marauders is a new object (CR 400.7) owned by the opponent; its ENTER ping hits the
        // opponent (we control it), and its earlier LTB ping hit us: 1 - 1 each.
        CHECK(s.players[0].life == 0);
        CHECK(s.players[1].life == 0);
    }
    CheckWorldsAgree(real, roll);
}

TEST_CASE("worldfire LTB: without an Athreos a coin mark is simply dropped with its creature")
{
    GameState base = LBase();
    LPut(base, "Keldon Halberdier", 25);
    base.athreos_coin_ids.push_back(25);
    GameState s = base;
    ResolveExileAllAndSetLife(s, LDef("Worldfire").params);
    CHECK(s.athreos_coin_ids.empty());
    CHECK(s.battlefield.empty());
    CHECK(InExile(s, 25) != nullptr);
}

TEST_CASE("Saproling Burst's LTB is in the shared hook: a bounce destroys the tokens it made")
{
    // Before 2026-11-22 the Burst's "destroy all tokens created with it" fired only at its own fading sacrifice.
    GameState s; s.active_player_index = 0; s.turn_number = 3;
    LPut(s, "Saproling Burst", 50);
    for (int i = 0; i < 2; ++i)
    {
        Permanent tok; tok.card.m_name = "Saproling"; tok.card.RehashName(); tok.card.AddType(CardType::Creature);
        tok.card.m_power = 1; tok.card.m_toughness = 1; tok.card.m_number = s.next_token_number++;
        tok.is_token = true; tok.controller_index = 0; tok.owner_index = 0; tok.created_by_number = 50;
        s.battlefield.push_back(tok);
    }
    LPut(s, "Mountain", 51);
    REQUIRE(ReturnPermanentToOwnersHand(s, 0, std::string()));
    REQUIRE(s.battlefield.size() == 1);
    CHECK(s.battlefield[0].card.m_name.str() == "Mountain");
    REQUIRE(s.players[0].hand.size() == 1);
    CHECK(s.players[0].hand[0].m_name.str() == "Saproling Burst");
}
