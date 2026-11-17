// 1 Drop Burn's two new cards (2026-11-17), in BOTH apply worlds:
//   * Needle Drop -- "deals 1 damage to any target that was dealt damage this turn. Draw a card."
//     (CardParams::target_dealt_damage_this_turn + cast_draw; GameState::player_dealt_damage_this_turn).
//   * Vexing Devil -- "When this creature enters, any opponent may have it deal 4 damage to them. If a
//     player does, sacrifice this creature." (CardParams::etb_opp_may_take_damage_to_sac; the PROVISIONAL
//     passive-opponent policy VexingDevilOpponentTakesDamage: take it unless it would be lethal).
#include <doctest/doctest.h>
#include "ai/AIEngine.h"
#include "ai/Combat.h"
#include "ai/DecisionProviders.h"
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
#include <vector>

namespace
{
const CardDefinition& NDef(const char* name)
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
Card NCard(const char* name, int number)
{
    Card c = NDef(name).card;
    c.m_number = number;
    return c;
}
Permanent& NPut(GameState& s, const char* name, int number, int who = 0)
{
    Permanent p;
    p.card = NCard(name, number);
    p.controller_index = p.owner_index = who;
    p.entered_this_turn = false;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}
// Mountains on the battlefield, `hand` in hand, a library of Mountains to draw from, opponent at `opp_life`.
GameState NBoard(int mountains, const std::vector<const char*>& hand, int opp_life = 20)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = opp_life;
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < mountains; ++k) { NPut(s, "Mountain", 70 + k); }
    int n = 1;
    for (const char* h : hand) { s.players[0].hand.push_back(NCard(h, n++)); }
    for (int k = 0; k < 5; ++k) { s.players[0].library.push_back(NCard("Mountain", 40 + k)); }
    return s;
}
Action NCast(const char* name)
{
    Action a;
    a.kind           = Action::Kind::CastFromHand;
    a.card_name      = name;
    a.def            = &NDef(name);
    a.hand_index     = -1;
    a.cost           = NDef(name).card.m_mana_cost;
    a.is_noncreature = !NDef(name).card.IsCreature();
    return a;
}
TurnSolver::Plan NPlan(const std::vector<Action>& acts) { TurnSolver::Plan p; p.actions = acts; return p; }
void NResolveAll(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}
// The executor's own hand cast of the named card, then the stack resolved.
void NExecCast(AIEngine& eng, GameState& s, const char* name)
{
    auto& hand = s.players[0].hand;
    auto it = std::find_if(hand.begin(), hand.end(), [&](const Card& c) { return c.m_name == name; });
    REQUIRE(it != hand.end());
    ManaPool avail = AvailableManaPool(s);
    MtgTestSeam::CastSpellFromHand(eng, s, *it, avail, 0);
    NResolveAll(s);
}
bool InHand(const GameState& s, const char* name)
{
    for (const Card& c : s.players[0].hand) { if (c.m_name == name) { return true; } }
    return false;
}
bool InGraveyard(const GameState& s, const char* name)
{
    for (const Card& c : s.players[0].graveyard) { if (c.m_name == name) { return true; } }
    return false;
}
bool OnBattlefield(const GameState& s, const char* name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name == name) { return true; } }
    return false;
}
int UntappedMountains(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name == "Mountain" && !p.tapped) { ++n; } }
    return n;
}
}   // namespace

TEST_CASE("Needle Drop: no target dealt damage this turn -> not castable, nothing paid (both worlds)")
{
    const GameState board = NBoard(1, {"Needle Drop"});
    CHECK_FALSE(DamagedTargetCastLegal(board, 0, NDef("Needle Drop")));
    CHECK_FALSE(FreeCastHasLegalTargets(board, 0, NDef("Needle Drop")));

    GameState roll = board;
    TurnSolver::ApplyPlan(roll, NPlan({ NCast("Needle Drop") }), /*is_pre_combat=*/true);
    CHECK(InHand(roll, "Needle Drop"));
    CHECK(UntappedMountains(roll) == 1);
    CHECK(roll.players[1].life == 20);
    CHECK(roll.players[0].hand.size() == 1);

    GameState exec = board;
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    NExecCast(eng, exec, "Needle Drop");
    CHECK(InHand(exec, "Needle Drop"));
    CHECK(UntappedMountains(exec) == 1);
    CHECK(exec.players[1].life == 20);
}

TEST_CASE("Needle Drop: LIFE LOSS is not damage -- the opponent having lost life does not make it a target")
{
    GameState s = NBoard(1, {"Needle Drop"});
    s.opponent_lost_life_this_turn = true;   // e.g. a drain: lost life, never dealt damage
    CHECK_FALSE(DamagedTargetCastLegal(s, 0, NDef("Needle Drop")));
    GameState exec = s;
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    NExecCast(eng, exec, "Needle Drop");
    CHECK(InHand(exec, "Needle Drop"));
}

TEST_CASE("Needle Drop: castable after a burn spell this turn -- 1 to the opponent and a card drawn (both worlds)")
{
    const GameState board = NBoard(2, {"Lightning Bolt", "Needle Drop"});

    GameState roll = board;
    TurnSolver::ApplyPlan(roll, NPlan({ NCast("Lightning Bolt"), NCast("Needle Drop") }), /*is_pre_combat=*/true);
    CHECK(roll.players[1].life == 16);
    CHECK(roll.player_dealt_damage_this_turn[1]);
    CHECK_FALSE(InHand(roll, "Needle Drop"));
    CHECK(InGraveyard(roll, "Needle Drop"));
    CHECK(roll.players[0].hand.size() == 1);          // the drawn Mountain
    CHECK(roll.players[0].cards_drawn_this_turn == 1);
    CHECK(roll.players[0].library.size() == 4);

    GameState exec = board;
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    NExecCast(eng, exec, "Lightning Bolt");
    CHECK(exec.players[1].life == 17);
    CHECK(exec.player_dealt_damage_this_turn[1]);
    CHECK(DamagedTargetCastLegal(exec, 0, NDef("Needle Drop")));
    NExecCast(eng, exec, "Needle Drop");
    CHECK(exec.players[1].life == 16);
    CHECK(InGraveyard(exec, "Needle Drop"));
    CHECK(exec.players[0].hand.size() == 1);
    CHECK(exec.players[0].cards_drawn_this_turn == 1);
    CHECK(exec.players[0].library.size() == 4);
}

TEST_CASE("Needle Drop: the plan order cannot strand it -- the deck's cast order puts it after the burn")
{
    GameState s = NBoard(2, {"Needle Drop", "Lightning Bolt"});
    static const OneDropBurnProvider provider; s.m_provider = &provider;
    Action nd = NCast("Needle Drop"), bolt = NCast("Lightning Bolt");
    CHECK(CastOrderLess(s, bolt, nd));
    CHECK_FALSE(CastOrderLess(s, nd, bolt));
    // Listed Needle-first, the rollout still realises Bolt then Needle Drop.
    TurnSolver::ApplyPlan(s, NPlan({ nd, bolt }), /*is_pre_combat=*/true);
    CHECK(s.players[1].life == 16);
    CHECK(InGraveyard(s, "Needle Drop"));
}

TEST_CASE("Needle Drop: castable after COMBAT damage; combat marks the opponent dealt damage")
{
    GameState s = NBoard(1, {"Needle Drop"});
    NPut(s, "Monastery Swiftspear", 20);
    const int idx = static_cast<int>(s.battlefield.size()) - 1;
    s.phase = Phase::Combat;
    ResolveCombatDamage(s, {idx}, 0, false);
    CHECK(s.players[1].life == 19);
    CHECK(s.player_dealt_damage_this_turn[1]);
    CHECK_FALSE(s.player_dealt_damage_this_turn[0]);
    s.phase = Phase::PostCombatMain;
    CHECK(DamagedTargetCastLegal(s, 0, NDef("Needle Drop")));

    GameState roll = s;
    TurnSolver::ApplyPlan(roll, NPlan({ NCast("Needle Drop") }), /*is_pre_combat=*/false);
    CHECK(roll.players[1].life == 18);
    CHECK(roll.players[0].cards_drawn_this_turn == 1);

    GameState exec = s;
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    NExecCast(eng, exec, "Needle Drop");
    CHECK(exec.players[1].life == 18);
    CHECK(exec.players[0].cards_drawn_this_turn == 1);
}

TEST_CASE("Needle Drop: the cast-time target list holds exactly the targets dealt damage this turn")
{
    GameState s = NBoard(1, {"Needle Drop"});
    NPut(s, "Monastery Swiftspear", 20);                 // ours, undamaged
    Permanent& hurt = NPut(s, "Goblin Guide", 21, 1);    // theirs, 1 damage marked
    hurt.damage = 1;
    NPut(s, "Goblin Guide", 22, 1);                      // theirs, undamaged
    const CardDefinition& nd = NDef("Needle Drop");
    auto has = [](const std::vector<FixedBurnPin>& v, int t)
    { return std::any_of(v.begin(), v.end(), [&](const FixedBurnPin& p) { return p.first == t; }); };
    const auto now = FixedBurnCandidates(s, 0, nd);
    CHECK(has(now, 21));
    CHECK_FALSE(has(now, 22));
    CHECK_FALSE(has(now, 20));
    CHECK_FALSE(has(now, 0));                            // the opponent was not dealt damage
    CHECK_FALSE(has(now, -1));
    CHECK(FreeCastHasLegalTargets(s, 0, nd));
    // The ENUMERATION list keeps every target: an earlier same-plan cast can still damage one.
    const auto pending = FixedBurnCandidates(s, 0, nd, /*pending=*/true);
    CHECK(has(pending, 0));
    CHECK(has(pending, 22));
    s.MarkPlayerDealtDamage(1, 3);
    CHECK(has(FixedBurnCandidates(s, 0, nd), 0));
    // A ChosenTarget's 2HG head index (2) is the same opposing team.
    GameState t = NBoard(1, {});
    t.MarkPlayerDealtDamage(2, 1);
    CHECK(t.player_dealt_damage_this_turn[1]);
    t.MarkPlayerDealtDamage(0, 0);                       // zero damage is no damage
    CHECK_FALSE(t.player_dealt_damage_this_turn[0]);
}

TEST_CASE("Vexing Devil: the opponent at high life TAKES 4 and the Devil is sacrificed (both worlds)")
{
    const GameState board = NBoard(1, {"Vexing Devil"});

    GameState roll = board;
    TurnSolver::ApplyPlan(roll, NPlan({ NCast("Vexing Devil") }), /*is_pre_combat=*/true);
    CHECK(roll.players[1].life == 16);
    CHECK(roll.opponent_lost_life_this_turn);
    CHECK(roll.player_dealt_damage_this_turn[1]);
    CHECK_FALSE(OnBattlefield(roll, "Vexing Devil"));
    CHECK(InGraveyard(roll, "Vexing Devil"));
    CHECK(roll.creature_died_this_turn);

    GameState exec = board;
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    NExecCast(eng, exec, "Vexing Devil");
    CHECK(exec.players[1].life == 16);
    CHECK(exec.player_dealt_damage_this_turn[1]);
    CHECK_FALSE(OnBattlefield(exec, "Vexing Devil"));
    CHECK(InGraveyard(exec, "Vexing Devil"));
}

TEST_CASE("Vexing Devil: the opponent at <= 4 DECLINES (it would be lethal) and the Devil stays a 4/3 (both worlds)")
{
    for (const int life : {4, 1})
    {
        CAPTURE(life);
        const GameState board = NBoard(1, {"Vexing Devil"}, life);
        GameState roll = board;
        TurnSolver::ApplyPlan(roll, NPlan({ NCast("Vexing Devil") }), /*is_pre_combat=*/true);
        CHECK(roll.players[1].life == life);
        CHECK(OnBattlefield(roll, "Vexing Devil"));
        CHECK_FALSE(roll.player_dealt_damage_this_turn[1]);

        GameState exec = board;
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        NExecCast(eng, exec, "Vexing Devil");
        CHECK(exec.players[1].life == life);
        CHECK(OnBattlefield(exec, "Vexing Devil"));
    }
    // At 5 the 4 is not lethal: taken (5 -> 1).
    GameState five = NBoard(1, {"Vexing Devil"}, 5);
    TurnSolver::ApplyPlan(five, NPlan({ NCast("Vexing Devil") }), /*is_pre_combat=*/true);
    CHECK(five.players[1].life == 1);
    CHECK_FALSE(OnBattlefield(five, "Vexing Devil"));
}

TEST_CASE("Vexing Devil's taken damage makes Needle Drop castable in the same main (it is cast after the Devil)")
{
    const GameState board = NBoard(2, {"Needle Drop", "Vexing Devil"});
    GameState roll = board;
    TurnSolver::ApplyPlan(roll, NPlan({ NCast("Needle Drop"), NCast("Vexing Devil") }), /*is_pre_combat=*/true);
    CHECK(roll.players[1].life == 15);
    CHECK(InGraveyard(roll, "Needle Drop"));
    CHECK(InGraveyard(roll, "Vexing Devil"));
}
