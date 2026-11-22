// Creature Burn Stage 2, batch 1 (2026-11-20), in BOTH apply worlds where a world applies:
//   * Keldon Champion  -- haste, echo {2}{R}{R}, ETB 3 to the opponent's face (etb_damage_any).
//   * Keldon Marauders -- VANISHING 2 (CardParams::vanishing_counters -> Permanent::time_counters,
//     PerformUpkeepVanishing) and "enters OR leaves: 1 damage" (etb_damage_any + ltb_damage_any).
//   * Flame Javelin    -- {2/R}{2/R}{2/R} (twobrid, MV 6) and the MONO-COLOUR FOLD
//     (MTG_TWOBRID_COLOUR_FOLD, default ON): an all-red board is offered only {R}{R}{R}.
//   * Browbeat         -- the punisher draw (opp_may_take_damage_instead_of_draw) and its PROVISIONAL
//     policy A (BrowbeatOpponentTakesDamage: take iff life - visible combat damage > 5 + (we hold a card ? 3 : 0)).
#include <doctest/doctest.h>
#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"
#include "runner/GoldFishRunner.h"
#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace
{
const CardDefinition& CbDef(const char* name)
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
Card CbCard(const char* name, int number)
{
    Card c = CbDef(name).card;
    c.m_number = number;
    return c;
}
Permanent& CbPut(GameState& s, const char* name, int number, bool sick = false)
{
    Permanent p;
    p.card = CbCard(name, number);
    p.controller_index = p.owner_index = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}
GameState CbBoard(int mountains, const std::vector<const char*>& hand, int opp_life = 20)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = opp_life;
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < mountains; ++k) { CbPut(s, "Mountain", 70 + k); }
    int n = 1;
    for (const char* h : hand) { s.players[0].hand.push_back(CbCard(h, n++)); }
    for (int k = 0; k < 8; ++k) { s.players[0].library.push_back(CbCard("Mountain", 40 + k)); }
    return s;
}
Action CbCast(const char* name)
{
    Action a;
    a.kind           = Action::Kind::CastFromHand;
    a.card_name      = name;
    a.def            = &CbDef(name);
    a.hand_index     = -1;
    a.cost           = CbDef(name).card.m_mana_cost;
    a.is_noncreature = !CbDef(name).card.IsCreature();
    return a;
}
TurnSolver::Plan CbPlan(const std::vector<Action>& acts) { TurnSolver::Plan p; p.actions = acts; return p; }
void CbResolveAll(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}
void CbExecCast(AIEngine& eng, GameState& s, const char* name)
{
    auto& hand = s.players[0].hand;
    auto it = std::find_if(hand.begin(), hand.end(), [&](const Card& c) { return c.m_name == name; });
    REQUIRE(it != hand.end());
    ManaPool avail = AvailableManaPool(s);
    MtgTestSeam::CastSpellFromHand(eng, s, *it, avail, 0);
    CbResolveAll(s);
}
const Permanent* CbFind(const GameState& s, const char* name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name == name) { return &p; } }
    return nullptr;
}
bool CbInGraveyard(const GameState& s, const char* name)
{
    for (const Card& c : s.players[0].graveyard) { if (c.m_name == name) { return true; } }
    return false;
}
struct CbHeads
{
    int saved;
    explicit CbHeads(int n) : saved(gamesetup::t_setup.opponent_heads) { gamesetup::t_setup.opponent_heads = n; }
    ~CbHeads() { gamesetup::t_setup.opponent_heads = saved; }
};
std::set<int> JavelinPaymentsOffered(const GameState& s)
{
    std::set<int> seen;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        for (const Action& a : p.actions)
        {
            if (a.card_name.str() != "Flame Javelin") { continue; }
            seen.insert(a.twobrid_colored);
            CHECK(a.cost.ManaValue() == 6 - a.twobrid_colored);
            CHECK(a.cost.red == a.twobrid_colored);
        }
    }
    return seen;
}
}   // namespace

// ---- Keldon Champion -------------------------------------------------------------------------------------

TEST_CASE("Keldon Champion: parsed as a hasty 3/2 with echo {2}{R}{R} and a 3-damage ETB")
{
    const CardDefinition& d = CbDef("Keldon Champion");
    CHECK(d.card.m_mana_cost.ManaValue() == 4);
    CHECK(d.card.m_mana_cost.red == 2);
    CHECK(d.card.HasKeyword(Keyword::Haste));
    REQUIRE(d.params.echo_cost.has_value());
    CHECK(d.params.echo_cost->red == 2);
    CHECK(d.params.echo_cost->generic == 2);
    CHECK(d.params.etb_damage_any == 3);
}

TEST_CASE("Keldon Champion: the ETB deals 3 to the opponent's face (both worlds)")
{
    const GameState board = CbBoard(4, {"Keldon Champion"});
    GameState roll = board;
    TurnSolver::ApplyPlan(roll, CbPlan({ CbCast("Keldon Champion") }), /*is_pre_combat=*/true);
    CHECK(roll.players[1].life == 17);
    CHECK(roll.opponent_lost_life_this_turn);
    REQUIRE(CbFind(roll, "Keldon Champion") != nullptr);

    GameState exec = board;
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    CbExecCast(eng, exec, "Keldon Champion");
    CHECK(exec.players[1].life == 17);
    REQUIRE(CbFind(exec, "Keldon Champion") != nullptr);
    CHECK_FALSE(CbFind(exec, "Keldon Champion")->echo_resolved);   // echo is owed at the next upkeep
}

// ---- Keldon Marauders ------------------------------------------------------------------------------------

TEST_CASE("Keldon Marauders: enters with two time counters and pings 1 (both worlds)")
{
    const CardDefinition& d = CbDef("Keldon Marauders");
    CHECK(d.params.vanishing_counters == 2);
    CHECK(d.params.etb_damage_any == 1);
    CHECK(d.params.ltb_damage_any == 1);

    const GameState board = CbBoard(2, {"Keldon Marauders"});
    GameState roll = board;
    TurnSolver::ApplyPlan(roll, CbPlan({ CbCast("Keldon Marauders") }), /*is_pre_combat=*/true);
    CHECK(roll.players[1].life == 19);
    REQUIRE(CbFind(roll, "Keldon Marauders") != nullptr);
    CHECK(CbFind(roll, "Keldon Marauders")->time_counters == 2);

    GameState exec = board;
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    CbExecCast(eng, exec, "Keldon Marauders");
    CHECK(exec.players[1].life == 19);
    REQUIRE(CbFind(exec, "Keldon Marauders") != nullptr);
    CHECK(CbFind(exec, "Keldon Marauders")->time_counters == 2);
}

TEST_CASE("Keldon Marauders: vanishing removes one per upkeep and sacrifices on the LAST -- the leave pings 1")
{
    // The executor's upkeep (GameEngine::UpkeepTail) calls the shared PerformUpkeepVanishing.
    GameState s = CbBoard(2, {});
    CbPut(s, "Keldon Marauders", 9).time_counters = 2;
    PerformUpkeepVanishing(s);
    REQUIRE(CbFind(s, "Keldon Marauders") != nullptr);
    CHECK(CbFind(s, "Keldon Marauders")->time_counters == 1);
    CHECK(s.players[1].life == 20);
    PerformUpkeepVanishing(s);
    CHECK(CbFind(s, "Keldon Marauders") == nullptr);
    CHECK(CbInGraveyard(s, "Keldon Marauders"));
    CHECK(s.players[1].life == 19);                 // the LEAVE half
    CHECK(s.creature_died_this_turn);

    // No time counter: the intervening "if" -- nothing happens, ever.
    GameState z = CbBoard(2, {});
    CbPut(z, "Keldon Marauders", 9).time_counters = 0;
    PerformUpkeepVanishing(z);
    CHECK(CbFind(z, "Keldon Marauders") != nullptr);
    CHECK(z.players[1].life == 20);
}

TEST_CASE("Keldon Marauders: the rollout's upkeep matches -- two turn boundaries, then gone with the ping")
{
    GameState s = CbBoard(2, {});
    CbPut(s, "Keldon Marauders", 9, /*sick=*/true).time_counters = 2;
    REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(s));
    REQUIRE(CbFind(s, "Keldon Marauders") != nullptr);
    CHECK(CbFind(s, "Keldon Marauders")->time_counters == 1);
    CHECK(s.players[1].life == 20);
    REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(s));
    CHECK(CbFind(s, "Keldon Marauders") == nullptr);
    CHECK(s.players[1].life == 19);
}

TEST_CASE("Keldon Marauders: a lethal upkeep leave trigger has killed the opponent by the start of that turn (rollout)")
{
    GameState s = CbBoard(2, {}, /*opp_life=*/1);
    CbPut(s, "Keldon Marauders", 9, /*sick=*/true).time_counters = 1;
    const int t0 = s.turn_number;
    TurnSolver::SimulateEndAndStartNextTurnForTest(s);
    CHECK(s.players[1].life <= 0);
    CHECK(OpponentHasLost(s));
    CHECK(s.turn_number == t0 + 1);
}

TEST_CASE("Keldon Marauders: any leave pings -- a sacrifice outlet's SacrificePermanentAt")
{
    GameState s = CbBoard(0, {});
    CbPut(s, "Keldon Marauders", 9).time_counters = 2;
    SacrificePermanentAt(s, 0, 0);
    CHECK(CbFind(s, "Keldon Marauders") == nullptr);
    CHECK(s.players[1].life == 19);
    CHECK(s.player_dealt_damage_this_turn[1]);
}

TEST_CASE("Keldon Marauders: a multi-put applies the time counters in its own pass (ApplyEntersWithOnPut)")
{
    GameState s = CbBoard(0, {});
    Permanent perm;
    perm.card = CbCard("Keldon Marauders", 9);
    perm.controller_index = perm.owner_index = 0;
    std::vector<int> done;
    ApplyEntersWithOnPut(perm, CbDef("Keldon Marauders"), /*pre_shift=*/1, done, s, s.battlefield.size());
    CHECK(perm.time_counters == 4);                 // a doubler (shift 1) doubles the as-enters counters
    CHECK(done == std::vector<int>{9});             // so FireEtbWatchers does not put them again
}

// ---- Flame Javelin ---------------------------------------------------------------------------------------

TEST_CASE("Flame Javelin: {2/R} x3 is MV 6, red, all generic, 4 damage to any target")
{
    const CardDefinition& d = CbDef("Flame Javelin");
    const ManaCost& c = d.card.m_mana_cost;
    CHECK(c.ManaValue() == 6);
    CHECK(c.generic == 6);
    CHECK(c.red == 0);
    CHECK(c.twobrid_count == 3);
    CHECK(d.card.HasColor(Color::Red));
    CHECK(d.tmpl == CardTemplate::DirectDamage);
    CHECK(d.params.damage == 4);
}

TEST_CASE("Flame Javelin: {R}{R}{R} off three Mountains deals 4 (rollout)")
{
    GameState s = CbBoard(3, {"Flame Javelin"});
    Action a = CbCast("Flame Javelin");
    a.cost.PayTwobridWithColor(3);
    a.twobrid_colored = 3;
    TurnSolver::ApplyPlan(s, CbPlan({ a }), /*is_pre_combat=*/true);
    CHECK(s.players[1].life == 16);
    CHECK(CbInGraveyard(s, "Flame Javelin"));
}

TEST_CASE("Flame Javelin fold: an all-Mountain board is offered ONLY the {R}{R}{R} payment")
{
    GameState s = CbBoard(6, {"Flame Javelin"});
    CHECK(JavelinPaymentsOffered(s) == std::set<int>{3});
    // A Mountain in hand changes nothing (it makes only {R}).
    GameState h = CbBoard(6, {"Flame Javelin", "Mountain"});
    CHECK(JavelinPaymentsOffered(h) == std::set<int>{3});
}

TEST_CASE("Flame Javelin fold: a {C} source keeps the whole fan (an Ancient Tomb's {C}{C} pays a {2} pip)")
{
    GameState s = CbBoard(4, {"Flame Javelin"});
    CbPut(s, "Ancient Tomb", 80);
    CHECK(JavelinPaymentsOffered(s) == std::set<int>{0, 1, 2, 3});
    // ...and so does a colourless land still in HAND (it could have been the turn's drop).
    GameState h = CbBoard(6, {"Flame Javelin", "Ancient Tomb"});
    h.players[0].lands_played_this_turn = 0;
    CHECK(JavelinPaymentsOffered(h) == std::set<int>{0, 1, 2, 3});
}

// ---- Browbeat --------------------------------------------------------------------------------------------

TEST_CASE("Browbeat: parsed as a 3-card draw with a 5-damage punisher; second-main relevant")
{
    const CardDefinition& d = CbDef("Browbeat");
    CHECK(d.tmpl == CardTemplate::DrawSpell);
    CHECK(d.params.draw == 3);
    CHECK(d.params.opp_may_take_damage_instead_of_draw == 5);
    CHECK(d.card.m_mana_cost.ManaValue() == 3);
    Decklist deck;
    deck.mainboard.push_back(CbCard("Browbeat", 1));
    deck.mainboard.push_back(CbCard("Mountain", 2));
    CHECK(GoldFishRunner::DeckUsesSecondMain(deck));
}

TEST_CASE("Browbeat: the opponent at high life TAKES 5 and nothing is drawn (both worlds)")
{
    const GameState board = CbBoard(3, {"Browbeat"});
    GameState roll = board;
    TurnSolver::ApplyPlan(roll, CbPlan({ CbCast("Browbeat") }), /*is_pre_combat=*/true);
    CHECK(roll.players[1].life == 15);
    CHECK(roll.player_dealt_damage_this_turn[1]);
    CHECK(roll.players[0].hand.empty());
    CHECK(roll.players[0].library.size() == 8);

    GameState exec = board;
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    CbExecCast(eng, exec, "Browbeat");
    CHECK(exec.players[1].life == 15);
    CHECK(exec.players[0].hand.empty());
    CHECK(exec.players[0].library.size() == 8);
    CHECK(CbInGraveyard(exec, "Browbeat"));
}

TEST_CASE("Browbeat: with our hand EMPTY the opponent declines at 5 and takes at 6 (both worlds)")
{
    for (const int life : {5, 6})
    {
        CAPTURE(life);
        const bool takes = life == 6;
        const GameState board = CbBoard(3, {"Browbeat"}, life);
        GameState roll = board;
        TurnSolver::ApplyPlan(roll, CbPlan({ CbCast("Browbeat") }), /*is_pre_combat=*/true);
        CHECK(roll.players[1].life == (takes ? 1 : life));
        CHECK(roll.players[0].hand.size() == (takes ? 0u : 3u));

        GameState exec = board;
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        CbExecCast(eng, exec, "Browbeat");
        CHECK(exec.players[1].life == (takes ? 1 : life));
        CHECK(exec.players[0].hand.size() == (takes ? 0u : 3u));
    }
}

TEST_CASE("Browbeat: while we HOLD a card the opponent declines at 8 and takes at 9; Browbeat itself is not counted")
{
    for (const int life : {8, 9})
    {
        CAPTURE(life);
        const bool takes = life == 9;
        // Three Mountains: only Browbeat is cast, the Lightning Bolt stays in hand.
        const GameState board = CbBoard(3, {"Browbeat", "Lightning Bolt"}, life);
        GameState roll = board;
        TurnSolver::ApplyPlan(roll, CbPlan({ CbCast("Browbeat") }), /*is_pre_combat=*/true);
        CHECK(roll.players[1].life == (takes ? 4 : life));
        CHECK(roll.players[0].hand.size() == (takes ? 1u : 4u));

        GameState exec = board;
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        CbExecCast(eng, exec, "Browbeat");
        CHECK(exec.players[1].life == (takes ? 4 : life));
        CHECK(exec.players[0].hand.size() == (takes ? 1u : 4u));
    }
}

TEST_CASE("Browbeat policy: the opponent subtracts the VISIBLE combat damage still to come (both worlds)")
{
    // USER 2026-11-22: the opponent takes the visible combat damage into account. Holding a card (threshold 8),
    // opponent at 10, a Keldon Champion (3, untapped, not sick) that can still attack: pre-combat it decides at
    // 10 - 3 = 7 and DECLINES (we draw three); after combat (visible 0) the same 10 TAKES.
    for (const bool post : {false, true})
    {
        CAPTURE(post);
        GameState board = CbBoard(3, {"Browbeat", "Lightning Bolt"}, /*opp_life=*/10);
        if (post) { board.phase = Phase::PostCombatMain; }
        CbPut(board, "Keldon Champion", 20);
        CHECK(OpponentVisibleDamageThisTurn(board, 0) == (post ? 0 : 3));
        GameState roll = board;
        TurnSolver::ApplyPlan(roll, CbPlan({ CbCast("Browbeat") }), /*is_pre_combat=*/!post);
        CHECK(roll.players[1].life == (post ? 5 : 10));
        CHECK(roll.players[0].hand.size() == (post ? 1u : 4u));

        GameState exec = board;
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        CbExecCast(eng, exec, "Browbeat");
        CHECK(exec.players[1].life == (post ? 5 : 10));
        CHECK(exec.players[0].hand.size() == (post ? 1u : 4u));
    }
}

TEST_CASE("Browbeat policy: what is visible -- sick, tapped, hasted, pumped, prevented, ready pings")
{
    GameState s = CbBoard(0, {});
    CbPut(s, "Keldon Champion", 20, /*sick=*/true);               // haste: counts the turn it lands
    CHECK(OpponentVisibleDamageThisTurn(s, 0) == 3);
    CbPut(s, "Goblin Fireslinger", 21, /*sick=*/true);            // summoning-sick, no haste: cannot attack
    CHECK(OpponentVisibleDamageThisTurn(s, 0) == 3);
    Permanent& t = CbPut(s, "Goblin Fireslinger", 22);            // ready 1/1
    CHECK(OpponentVisibleDamageThisTurn(s, 0) == 4);
    t.tapped = true;                                                // tapped: cannot attack
    CHECK(OpponentVisibleDamageThisTurn(s, 0) == 3);
    s.battlefield.front().temp_power_bonus = 2;                     // (a Mountain-less board: front = Champion) pumped
    CHECK(OpponentVisibleDamageThisTurn(s, 0) == 5);
    s.combat_damage_prevented_this_turn = true;
    CHECK(OpponentVisibleDamageThisTurn(s, 0) == 0);
    s.combat_damage_prevented_this_turn = false;
    s.active_player_index = 1;                                      // not our turn: nothing is coming
    CHECK(OpponentVisibleDamageThisTurn(s, 0) == 0);
    s.active_player_index = 0;
    s.phase = Phase::PostCombatMain;                                // combat is over...
    CHECK(OpponentVisibleDamageThisTurn(s, 0) == 0);
    t.tapped = false;                                               // ...but a READY pinger's ping is still visible
    CHECK(OpponentVisibleDamageThisTurn(s, 0) == 1);
    t.tapped = true;
    s.phase = Phase::PreCombatMain;
    CbPut(s, "Cinder Pyromancer", 23);                              // a ready 0/1 pinger: its ping (1), not its attack (0)
    CHECK(OpponentVisibleDamageThisTurn(s, 0) == 6);
    s.battlefield.back().tapped = true;
    s.players[1].life = 11;                                         // 11 - 5 = 6: takes with our hand empty, declines holding
    CHECK(BrowbeatOpponentTakesDamage(s, 1, 5, false));
    CHECK_FALSE(BrowbeatOpponentTakesDamage(s, 1, 5, true));
}

TEST_CASE("Browbeat policy: staged (exiled-playable) cards are not held; 2HG reads the team's life and deals x1")
{
    GameState s = CbBoard(0, {});
    Card staged = CbCard("Lightning Bolt", 5);
    staged.m_is_staged = true;
    s.players[0].hand.push_back(staged);
    CHECK(HeldCardCount(s, 0) == 0);
    s.players[1].life = 6;
    CHECK(BrowbeatOpponentTakesDamage(s, 1, 5, HeldCardCount(s, 0) > 0));

    CbHeads heads(2);
    GameState t = CbBoard(3, {"Browbeat"}, /*opp_life=*/30);
    TurnSolver::ApplyPlan(t, CbPlan({ CbCast("Browbeat") }), /*is_pre_combat=*/true);
    CHECK(t.players[1].life == 25);                 // ONE 5-damage hit to the shared team life
}
