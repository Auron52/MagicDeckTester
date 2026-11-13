// Boars I2a (2026-11-13): Zhur-Taa Druid -- "{T}: Add {G}. Whenever you tap this creature for mana, it deals 1
// damage to each opponent." (tap_mana_damage_each_opponent). The ping at every tap-for-mana choke point (the greedy
// TapSourceIntoFloat, the backtracking payer, the mana cache's hit replay -- whose drip-only replay would have
// turned the ping into a GAIN), the end-step free-ping fold and the post-combat lethal finish.
#include <doctest/doctest.h>
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include <string>

namespace
{
const CardDefinition& ZDef(const char* name)
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, name);
    return *d;
}
Permanent& ZPut(GameState& s, const char* name, int number, bool sick = false)
{
    Permanent p;
    p.card = ZDef(name).card;
    p.card.m_number = number;
    p.controller_index = p.owner_index = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}
GameState ZBase()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    return s;
}
ManaCost G1() { ManaCost c; c.green = 1; return c; }
}   // namespace

TEST_CASE("Zhur-Taa Druid: card data is the mana_dork shape with the new ping param")
{
    const CardDefinition& d = ZDef("Zhur-Taa Druid");
    CHECK(d.tmpl == CardTemplate::ManaDork);
    CHECK(d.params.tap_mana_damage_each_opponent == 1);
    CHECK(d.params.tap_opponent_lifegain == 0);
    CHECK(d.card.m_power == 1);
    CHECK(d.card.m_toughness == 1);
}

TEST_CASE("Zhur-Taa Druid: a tap for mana pings in the greedy tap helper and the backtracking payer")
{
    SUBCASE("TapSourceIntoFloat (the greedy payer's / the human pre-tap's one tap mechanic)")
    {
        GameState s = ZBase();
        ZPut(s, "Zhur-Taa Druid", 101);
        ManaPool fl;
        TapSourceIntoFloat(s, 0, s.battlefield[0], ZDef("Zhur-Taa Druid"), Color::Green, fl, nullptr, false, nullptr);
        CHECK(s.battlefield[0].tapped);
        CHECK(fl.green == 1);
        CHECK(s.players[1].life == 19);
        CHECK(s.opponent_lost_life_this_turn);
    }
    SUBCASE("TapForCostBacktrack pays {G} off the Druid: opponent 20 -> 19")
    {
        ManaCacheClearPerGame();
        GameState s = ZBase();
        ZPut(s, "Zhur-Taa Druid", 101);
        ManaPool leftover;
        REQUIRE(TapForCostBacktrack(s, G1(), /*for_creature=*/false, ManaPool{}, nullptr, nullptr, &leftover));
        CHECK(s.battlefield[0].tapped);
        CHECK(s.players[1].life == 19);
        ManaCacheClearPerGame();
    }
    SUBCASE("a FAILED backtracking branch refunds the ping (opp life + flag restored)")
    {
        GameState s = ZBase();
        ZPut(s, "Zhur-Taa Druid", 101);
        ManaCost gg; gg.green = 2;   // one Druid cannot pay {G}{G}
        ManaPool leftover;
        CHECK_FALSE(TapForCostBacktrack(s, gg, false, ManaPool{}, nullptr, nullptr, &leftover));
        CHECK_FALSE(s.battlefield[0].tapped);
        CHECK(s.players[1].life == 20);
        CHECK_FALSE(s.opponent_lost_life_this_turn);
    }
    SUBCASE("a summoning-sick Druid cannot pay (and so cannot ping)")
    {
        GameState s = ZBase();
        ZPut(s, "Zhur-Taa Druid", 101, /*sick=*/true);
        ManaPool leftover;
        CHECK_FALSE(TapForCostBacktrack(s, G1(), false, ManaPool{}, nullptr, nullptr, &leftover));
        CHECK(s.players[1].life == 20);
    }
}

// THE SIGN HAZARD: the cache stored the solve's whole opponent-life diff as the Grove drip and replayed it through
// OpponentGainsLife, so a cached Druid tap made the opponent GAIN 1 (20 -> 21) instead of losing it.
TEST_CASE("Zhur-Taa Druid: a mana-cache HIT replays the ping as damage, not as drip lifegain")
{
    ManaCacheClearPerGame();
    GameState board = ZBase();
    ZPut(board, "Zhur-Taa Druid", 101);
    ZPut(board, "Forest", 102);
    ZPut(board, "Forest", 103);
    ManaCost cost; cost.generic = 2; cost.green = 1;   // {2}{G}: every source taps, the Druid included
    {
        GameState a = board;
        ManaPool leftover, full;
        REQUIRE(TapForCostBacktrack(a, cost, false, ManaPool{}, nullptr, nullptr, &leftover, 0, -1, 0, &full));
        CHECK(a.players[1].life == 19);
    }
    REQUIRE(ManaCacheSizeForTest() >= 1);   // the solve was stored
    {
        GameState b = board;
        ManaPool leftover, full;
        REQUIRE(TapForCostBacktrack(b, cost, false, ManaPool{}, nullptr, nullptr, &leftover, 0, -1, 0, &full));   // the hit
        for (const Permanent& p : b.battlefield) { CHECK(p.tapped); }
        CHECK(b.players[1].life == 19);   // NOT 21
        CHECK(b.opponent_lost_life_this_turn);
    }
    SUBCASE("a Druid + Grove solve: the drip and the ping are separated (store and hit agree)")
    {
        ManaCacheClearPerGame();
        GameState g = ZBase();
        ZPut(g, "Zhur-Taa Druid", 101);
        ZPut(g, "Grove of the Burnwillows", 102);
        ManaCost rg; rg.red = 1; rg.green = 1;   // the Grove must make {R} (drip +1), the Druid {G} (ping -1)
        GameState a = g, b = g;
        ManaPool l1, f1, l2, f2;
        REQUIRE(TapForCostBacktrack(a, rg, false, ManaPool{}, nullptr, nullptr, &l1, 0, -1, 0, &f1));
        REQUIRE(TapForCostBacktrack(b, rg, false, ManaPool{}, nullptr, nullptr, &l2, 0, -1, 0, &f2));
        CHECK(a.players[1].life == 20);
        CHECK(b.players[1].life == a.players[1].life);
    }
    ManaCacheClearPerGame();
}

TEST_CASE("Zhur-Taa Druid: 2HG -- one tap deals 2 to the shared team total")
{
    const gamesetup::Setup saved = gamesetup::t_setup;
    gamesetup::t_setup.opponent_heads = 2;
    REQUIRE(gamesetup::OpponentHeads() == 2);
    GameState s = ZBase();
    s.players[1].life = 30;
    ZPut(s, "Zhur-Taa Druid", 101);
    ManaPool fl;
    TapSourceIntoFloat(s, 0, s.battlefield[0], ZDef("Zhur-Taa Druid"), Color::Green, fl, nullptr, false, nullptr);
    CHECK(s.players[1].life == 28);
    gamesetup::t_setup = saved;
}

TEST_CASE("Zhur-Taa Druid: the end-step free ping (a dominance fold, both worlds' shared helper)")
{
    SUBCASE("an untapped, able Druid is tapped for its ping; a tapped one (it attacked) is not")
    {
        GameState s = ZBase();
        ZPut(s, "Zhur-Taa Druid", 101);
        ZPut(s, "Zhur-Taa Druid", 102).tapped = true;
        CHECK(PingDorkEndSweep(s, 0) == 1);
        CHECK(s.battlefield[0].tapped);
        CHECK(s.players[1].life == 19);
    }
    SUBCASE("summoning-sick: not swept; with haste: swept")
    {
        GameState s = ZBase();
        ZPut(s, "Zhur-Taa Druid", 101, /*sick=*/true);
        CHECK(PingDorkEndSweep(s, 0) == 0);
        CHECK(s.players[1].life == 20);
        s.battlefield[0].temp_haste = true;
        CHECK(PingDorkEndSweep(s, 0) == 1);
        CHECK(s.players[1].life == 19);
    }
    SUBCASE("the rollout's end step: a non-lethal sweep pings and the next turn starts")
    {
        GameState s = ZBase();
        ZPut(s, "Zhur-Taa Druid", 101);
        for (int i = 0; i < 5; ++i)   // the next turn's draw needs a library
        { Card c = ZDef("Forest").card; c.m_number = 200 + i; s.players[0].library.push_back(c); }
        REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(s));
        CHECK(s.players[1].life == 19);
        CHECK(s.turn_number == 5);
    }
    SUBCASE("the rollout's end step: a LETHAL sweep wins THIS turn (the counter does not advance)")
    {
        GameState s = ZBase();
        s.players[1].life = 1;
        ZPut(s, "Zhur-Taa Druid", 101);
        REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(s));
        CHECK(s.players[1].life == 0);
        CHECK(OpponentHasLost(s));
        CHECK(s.turn_number == 4);
    }
    SUBCASE("no ping dork: the sweep is a no-op")
    {
        GameState s = ZBase();
        ZPut(s, "Birds of Paradise", 101);
        CHECK(PingDorkEndSweep(s, 0) == 0);
        CHECK_FALSE(s.battlefield[0].tapped);
    }
}

TEST_CASE("Zhur-Taa Druid: the post-combat lethal finish fires only when the pings cover the remaining life")
{
    SUBCASE("two Druids vs 2 life: both tapped, opponent dead")
    {
        GameState s = ZBase();
        s.players[1].life = 2;
        ZPut(s, "Zhur-Taa Druid", 101);
        ZPut(s, "Zhur-Taa Druid", 102);
        PingDorkFinish(s, 0);
        CHECK(s.players[1].life == 0);
    }
    SUBCASE("two Druids vs 3 life: nothing taps (the end-step sweep still pings later)")
    {
        GameState s = ZBase();
        s.players[1].life = 3;
        ZPut(s, "Zhur-Taa Druid", 101);
        ZPut(s, "Zhur-Taa Druid", 102);
        PingDorkFinish(s, 0);
        CHECK(s.players[1].life == 3);
        CHECK_FALSE(s.battlefield[0].tapped);
        CHECK_FALSE(s.battlefield[1].tapped);
    }
    SUBCASE("a Druid untapped again (Raggadragga) pings again: no once-per-turn assumption")
    {
        GameState s = ZBase();
        ZPut(s, "Zhur-Taa Druid", 101);
        ManaPool fl;
        TapSourceIntoFloat(s, 0, s.battlefield[0], ZDef("Zhur-Taa Druid"), Color::Green, fl, nullptr, false, nullptr);
        s.battlefield[0].tapped = false;
        TapSourceIntoFloat(s, 0, s.battlefield[0], ZDef("Zhur-Taa Druid"), Color::Green, fl, nullptr, false, nullptr);
        CHECK(s.players[1].life == 18);
    }
}
