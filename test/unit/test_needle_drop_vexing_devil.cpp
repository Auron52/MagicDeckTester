// 1 Drop Burn's two new cards (2026-11-17), in BOTH apply worlds:
//   * Needle Drop -- "deals 1 damage to any target that was dealt damage this turn. Draw a card."
//     (CardParams::target_dealt_damage_this_turn + cast_draw; GameState::player_dealt_damage_this_turn).
//   * Vexing Devil -- "When this creature enters, any opponent may have it deal 4 damage to them. If a
//     player does, sacrifice this creature." (CardParams::etb_opp_may_take_damage_to_sac; the USER's
//     2026-11-17 passive-opponent policy VexingDevilOpponentTakesDamage: decline at life <= 4 with our
//     hand empty, or <= 7 while we hold any card; take it otherwise).
#include <doctest/doctest.h>
#include "ai/AIEngine.h"
#include "ai/Combat.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
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

TEST_CASE("Vexing Devil: with our hand EMPTY the opponent at <= 4 DECLINES (it would be lethal) and the Devil stays a 4/3 (both worlds)")
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

TEST_CASE("Vexing Devil: while we HOLD a card the opponent declines at <= 7 and takes at 8 (both worlds)")
{
    // USER 2026-11-17: "unless they are at 4 or less when you have no cards in hand or 7 or less if you
    // are holding cards". One Mountain, so only the Devil is cast and the Lava Spike stays in hand.
    for (const int life : {7, 5})
    {
        CAPTURE(life);
        const GameState board = NBoard(1, {"Vexing Devil", "Lava Spike"}, life);
        GameState roll = board;
        TurnSolver::ApplyPlan(roll, NPlan({ NCast("Vexing Devil") }), /*is_pre_combat=*/true);
        CHECK(roll.players[1].life == life);
        CHECK(OnBattlefield(roll, "Vexing Devil"));

        GameState exec = board;
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        NExecCast(eng, exec, "Vexing Devil");
        CHECK(exec.players[1].life == life);
        CHECK(OnBattlefield(exec, "Vexing Devil"));
    }
    const GameState board8 = NBoard(1, {"Vexing Devil", "Lava Spike"}, 8);
    GameState roll = board8;
    TurnSolver::ApplyPlan(roll, NPlan({ NCast("Vexing Devil") }), /*is_pre_combat=*/true);
    CHECK(roll.players[1].life == 4);
    CHECK_FALSE(OnBattlefield(roll, "Vexing Devil"));

    GameState exec = board8;
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    NExecCast(eng, exec, "Vexing Devil");
    CHECK(exec.players[1].life == 4);
    CHECK_FALSE(OnBattlefield(exec, "Vexing Devil"));
}

TEST_CASE("Vexing Devil: the opponent subtracts the VISIBLE damage still to come; the Devil itself is not counted (both worlds)")
{
    // USER 2026-11-22: "Vexing Devil should act the same way" (as Browbeat: life less the visible damage). Holding a
    // card (decline at <= 7), opponent at 9, two untapped Swiftspears that can still attack: pre-combat it decides at
    // 9 - 2 = 7 and DECLINES (the Devil stays); after combat (nothing more to see) the same 9 TAKES (-> 5).
    for (const bool post : {false, true})
    {
        CAPTURE(post);
        GameState board = NBoard(1, {"Vexing Devil", "Lava Spike"}, 9);
        if (post) { board.phase = Phase::PostCombatMain; }
        NPut(board, "Monastery Swiftspear", 20);
        NPut(board, "Monastery Swiftspear", 21);
        CHECK(OpponentVisibleDamageThisTurn(board, 0) == (post ? 0 : 2));
        GameState roll = board;
        TurnSolver::ApplyPlan(roll, NPlan({ NCast("Vexing Devil") }), /*is_pre_combat=*/!post);
        CHECK(roll.players[1].life == (post ? 5 : 9));
        CHECK(OnBattlefield(roll, "Vexing Devil") == !post);

        GameState exec = board;
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        NExecCast(eng, exec, "Vexing Devil");
        CHECK(exec.players[1].life == (post ? 5 : 9));
        CHECK(OnBattlefield(exec, "Vexing Devil") == !post);
    }
    // The entering Devil is excluded even when it could attack (a hasted Devil's attack hangs on this very choice).
    GameState s = NBoard(0, {}, 9);
    Permanent& dv = NPut(s, "Vexing Devil", 30);
    dv.temp_haste = true;
    CHECK(OpponentVisibleDamageThisTurn(s, 0) == 4);
    CHECK(OpponentVisibleDamageThisTurn(s, 0, /*exclude_number=*/30) == 0);
    CHECK(VexingDevilOpponentTakesDamage(s, 0, 1, 4, /*devil_number=*/30) == (9 > 4));
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

// ---- the USER's amended order + main-2 rule (2026-11-18, MTG_ONEDROPBURN_ORDER) -----------------------------
namespace
{
struct OrderPin
{
    explicit OrderPin(bool on) { heurarm::t_arm[heurarm::ONEDROPBURN_ORDER] = on ? 1 : 0; }
    ~OrderPin() { heurarm::t_arm[heurarm::ONEDROPBURN_ORDER] = -1; }
};
const OneDropBurnProvider& OdbProv() { static const OneDropBurnProvider p; return p; }
Action NCastAt(const char* name, int hand_index) { Action a = NCast(name); a.hand_index = hand_index; return a; }
// Post-combat board: our hand as given, the deck's provider attached and its noncombat tracking armed.
GameState NPostCombat(const std::vector<const char*>& hand)
{
    GameState s = NBoard(3, hand);
    s.m_provider = &OdbProv();
    s.deck_m2_tracks_noncombat = true;
    s.phase = Phase::PostCombatMain;
    return s;
}
}   // namespace

TEST_CASE("1 Drop Burn order: Needle Drop and Skewer the Critics rank LAST but Fireblast, in either main")
{
    OrderPin on(true);
    // The USER's 2026-11-18 order, Fireblast LAST: MTG_ONEDROPBURN_FIREBLAST_EARLY (default ON since 2026-11-20) is
    // pinned OFF here; the early rank has its own test below.
    struct EarlyOff
    {
        EarlyOff() { heurarm::t_arm[heurarm::ONEDROPBURN_FIREBLAST_EARLY] = 0; }
        ~EarlyOff() { heurarm::t_arm[heurarm::ONEDROPBURN_FIREBLAST_EARLY] = -1; }
    } early_off;
    GameState s = NBoard(2, {});
    s.m_provider = &OdbProv();
    for (const Phase ph : { Phase::PreCombatMain, Phase::PostCombatMain })
    {
        s.phase = ph;
        s.player_dealt_damage_this_turn[1] = GameState::kDealtDamage;   // the old main-2 tier keyed on this
        CHECK(OdbProv().CastOrderRank(s, NDef("Needle Drop")) == 29);
        CHECK(OdbProv().CastOrderRank(s, NDef("Skewer the Critics")) == 30);
        CHECK(OdbProv().CastOrderRank(s, NDef("Lightning Bolt")) < 29);
        CHECK(OdbProv().CastOrderRank(s, NDef("Rift Bolt")) < 29);
        CHECK(OdbProv().CastOrderRank(s, NDef("Fireblast")) == 40);
    }
    // ONE CARD PER RANK (USER 2026-11-18): the twelve spells hold twelve distinct, increasing positions.
    const char* order[] = { "Goblin Guide", "Monastery Swiftspear", "Vexing Devil", "Grim Lavamancer", "Boltwave",
                            "Chain Lightning", "Lava Spike", "Lightning Bolt", "Rift Bolt", "Needle Drop",
                            "Skewer the Critics", "Fireblast" };
    for (std::size_t i = 1; i < sizeof(order) / sizeof(order[0]); ++i)
    { CHECK(OdbProv().CastOrderRank(s, NDef(order[i - 1])) < OdbProv().CastOrderRank(s, NDef(order[i]))); }
}

TEST_CASE("1 Drop Burn: combat damage marks only DEALT; noncombat damage also marks NONCOMBAT, only when armed")
{
    GameState s = NBoard(1, {"Lightning Bolt"});
    NPut(s, "Monastery Swiftspear", 20);
    const int idx = static_cast<int>(s.battlefield.size()) - 1;
    s.deck_m2_tracks_noncombat = true;
    s.phase = Phase::Combat;
    ResolveCombatDamage(s, {idx}, 0, false);
    CHECK(s.player_dealt_damage_this_turn[1]);
    CHECK_FALSE(s.PlayerDealtNoncombatDamage(1));

    // A Bolt, in both worlds, marks noncombat damage.
    GameState roll = NBoard(1, {"Lightning Bolt"});
    roll.deck_m2_tracks_noncombat = true;
    TurnSolver::ApplyPlan(roll, NPlan({ NCast("Lightning Bolt") }), /*is_pre_combat=*/true);
    CHECK(roll.players[1].life == 17);
    CHECK(roll.PlayerDealtNoncombatDamage(1));
    GameState exec = NBoard(1, {"Lightning Bolt"});
    exec.deck_m2_tracks_noncombat = true;
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    NExecCast(eng, exec, "Lightning Bolt");
    CHECK(exec.players[1].life == 17);
    CHECK(exec.PlayerDealtNoncombatDamage(1));

    // Not armed (every other deck, and this one with the order off): the bit is never written.
    GameState off = NBoard(1, {"Lightning Bolt"});
    TurnSolver::ApplyPlan(off, NPlan({ NCast("Lightning Bolt") }), /*is_pre_combat=*/true);
    CHECK(off.player_dealt_damage_this_turn[1] == GameState::kDealtDamage);
}

TEST_CASE("1 Drop Burn main-2 rule: Needle Drop / Skewer only when combat is the only damage; then only what Needle Drop drew")
{
    {
        OrderPin off(false);
        CHECK_FALSE(OdbProv().RestrictsMain2Casts());
        CHECK_FALSE(OdbProv().Main2RuleReadsNoncombatDamage());
    }
    OrderPin on(true);
    CHECK(OdbProv().RestrictsMain2Casts());
    CHECK(OdbProv().Main2RuleReadsNoncombatDamage());

    GameState s = NPostCombat({"Needle Drop", "Skewer the Critics", "Lightning Bolt", "Goblin Guide"});
    auto allowed = [&](const GameState& st, const char* name, int hi) { return OdbProv().Main2CastAllowed(st, NCastAt(name, hi)); };
    // No damage at all: nothing is admitted (Needle Drop has no target; a hardcast Skewer belongs before combat).
    CHECK_FALSE(allowed(s, "Needle Drop", 0));
    CHECK_FALSE(allowed(s, "Skewer the Critics", 1));
    CHECK_FALSE(allowed(s, "Lightning Bolt", 2));
    CHECK_FALSE(allowed(s, "Goblin Guide", 3));

    // Combat damage only: the two are admitted, nothing else.
    s.MarkPlayerDealtDamage(1, 2, /*combat=*/true);
    CHECK(allowed(s, "Needle Drop", 0));
    CHECK(allowed(s, "Skewer the Critics", 1));
    CHECK_FALSE(allowed(s, "Lightning Bolt", 2));
    CHECK_FALSE(allowed(s, "Goblin Guide", 3));

    // A NONCOMBAT source earlier this turn (a main-1 burn, Vexing Devil, an upkeep Rift Bolt): they were
    // castable before the attack, so main 2 does not offer them.
    GameState pre = s;
    pre.MarkPlayerDealtDamage(1, 3);
    CHECK_FALSE(allowed(pre, "Needle Drop", 0));
    CHECK_FALSE(allowed(pre, "Skewer the Critics", 1));

    // The main-2 Needle Drop stamps the hand; its own 1 damage is noncombat, yet Skewer stays admitted.
    NoteMain2StagerCast(s, NDef("Needle Drop"));
    CHECK(s.m2_stage_turn == s.turn_number);
    s.players[0].hand.erase(s.players[0].hand.begin());   // Needle Drop left the hand
    s.MarkPlayerDealtDamage(1, 1);                          // its 1 damage
    CHECK(allowed(s, "Skewer the Critics", 0));
    CHECK_FALSE(allowed(s, "Lightning Bolt", 1));           // held since before main 2
    CHECK_FALSE(allowed(s, "Goblin Guide", 2));
    // What it drew is admitted -- a second Bolt too, whichever copy's index the collect names.
    s.players[0].hand.push_back(NCard("Lightning Bolt", 30));
    CHECK(allowed(s, "Lightning Bolt", 1));
    CHECK(allowed(s, "Lightning Bolt", 3));
    s.players[0].hand.push_back(NCard("Chain Lightning", 31));
    CHECK(allowed(s, "Chain Lightning", 4));
    CHECK_FALSE(allowed(s, "Goblin Guide", 2));

    // A main-2 Skewer first also stamps, and a later Needle Drop's draw is still "new".
    GameState k = NPostCombat({"Skewer the Critics", "Needle Drop", "Lava Spike"});
    k.MarkPlayerDealtDamage(1, 2, /*combat=*/true);
    NoteMain2StagerCast(k, NDef("Skewer the Critics"));
    CHECK(k.m2_stage_turn == k.turn_number);
    CHECK(allowed(k, "Needle Drop", 1));
    CHECK_FALSE(allowed(k, "Lava Spike", 2));
    k.players[0].hand.push_back(NCard("Boltwave", 32));
    CHECK(allowed(k, "Boltwave", 3));

    // Rule off: a Needle Drop cast never stamps.
    OrderPin off(false);
    GameState t = NPostCombat({"Needle Drop"});
    NoteMain2StagerCast(t, NDef("Needle Drop"));
    CHECK(t.m2_stage_turn == -1);
}

// ---- the USER's activation rules (2026-11-18, MTG_ONEDROPBURN_ORDER): Lavamancer before Needle Drop / Skewer and
// main 1 only; Sunbaked Canyon's sacrifice-draw main 1 only ------------------------------------------------------
namespace
{
Action NLavaPing(int source)
{
    Action lava;
    lava.kind = Action::Kind::GraveyardExileAbility;
    lava.card_name = "Grim Lavamancer"; lava.def = &NDef("Grim Lavamancer");
    lava.sac_source_id = source; lava.gy_exile_mode = 3; lava.cost.red = 1;
    lava.tutor_target = GraveyardDamageFuelSpec("Lightning Bolt", "Mountain");
    lava.enchant_target = 0; lava.direct_damage = 2;
    return lava;
}
// Lavamancer (not sick) + 3 Mountains, two fuel cards in the yard, Needle Drop in hand; the plan lists Needle Drop
// BEFORE the ping (the trailing activation pass is where a plan's activations run).
GameState NLavaBoard()
{
    GameState s = NBoard(3, {"Needle Drop"});
    s.m_provider = &OdbProv();
    NPut(s, "Grim Lavamancer", 60);
    s.players[0].graveyard = { NCard("Lightning Bolt", 50), NCard("Mountain", 51) };
    return s;
}
}   // namespace

TEST_CASE("1 Drop Burn: Lavamancer's ping is hoisted before Needle Drop, so Needle Drop is castable (order ON only)")
{
    {
        OrderPin on(true);
        CHECK(OdbProv().HoistsFaceGraveyardDamage(NBoard(0, {})));
        GameState s = NLavaBoard();
        const std::vector<Action> acts = { NCastAt("Needle Drop", 0), NLavaPing(60) };
        CHECK(TurnSolver::SpectacleGraveyardDamageFirst(s, acts) == 1);
        TurnSolver::ApplyPlan(s, NPlan(acts), /*is_pre_combat=*/true);
        CHECK(s.players[1].life == 17);           // 2 from the ping, then Needle Drop's 1
        CHECK(InGraveyard(s, "Needle Drop"));
        CHECK_FALSE(InHand(s, "Needle Drop"));
    }
    {
        OrderPin off(false);
        CHECK_FALSE(OdbProv().HoistsFaceGraveyardDamage(NBoard(0, {})));
        GameState s = NLavaBoard();
        const std::vector<Action> acts = { NCastAt("Needle Drop", 0), NLavaPing(60) };
        CHECK(TurnSolver::SpectacleGraveyardDamageFirst(s, acts) == -1);   // no spectacle cast: not hoisted
        TurnSolver::ApplyPlan(s, NPlan(acts), /*is_pre_combat=*/true);
        CHECK(s.players[1].life == 18);           // the ping trails; Needle Drop had no target when it came up
        CHECK(InHand(s, "Needle Drop"));
    }
}

TEST_CASE("1 Drop Burn: Lavamancer is never offered after combat; Sunbaked Canyon's dig is closed after combat (order ON)")
{
    OrderPin on(true);
    CHECK(OdbProv().RestrictsCollectedActions());
    GameState s = NBoard(2, {});
    const Action ping = NLavaPing(60);
    CHECK(OdbProv().CollectedActionAllowed(s, ping, /*is_pre_combat=*/true));
    CHECK_FALSE(OdbProv().CollectedActionAllowed(s, ping, /*is_pre_combat=*/false));
    CHECK(OdbProv().CollectedActionAllowed(s, NCast("Lightning Bolt"), /*is_pre_combat=*/false));

    // Canyon: 3 lands besides it, nothing castable -> the generic gate passes in main 1, and is closed in main 2.
    GameState c = NBoard(3, {});
    c.m_provider = &OdbProv();
    NPut(c, "Sunbaked Canyon", 61);
    c.phase = Phase::PreCombatMain;
    const bool main1 = OdbProv().ShouldConsiderDig(c);
    CHECK(main1 == GenericProvider().ShouldConsiderDig(c));
    CHECK(main1);
    CHECK(OdbProv().DigOfferAt(c).Any() == GenericProvider().DigOfferAt(c).Any());
    c.phase = Phase::PostCombatMain;
    CHECK_FALSE(OdbProv().ShouldConsiderDig(c));
    CHECK_FALSE(OdbProv().DigOfferAt(c).Any());

    OrderPin off(false);
    CHECK_FALSE(OdbProv().RestrictsCollectedActions());
    CHECK(OdbProv().ShouldConsiderDig(c) == GenericProvider().ShouldConsiderDig(c));
}

TEST_CASE("Midrange Burn: Fireblast ranks after the other burn and before the spectacle spells (lever ON only)")
{
    static const MidrangeBurnProvider mb;
    GameState s = NBoard(2, {});
    s.m_provider = &mb;
    {
        heurarm::t_arm[heurarm::MIDRANGE_FIREBLAST_ORDER] = 1;
        CHECK(mb.CastOrderRank(s, NDef("Fireblast")) == 25);
        CHECK(mb.CastOrderRank(s, NDef("Lightning Bolt")) < 25);
        CHECK(mb.CastOrderRank(s, NDef("Rift Bolt")) < 25);
        CHECK(mb.CastOrderRank(s, NDef("Skewer the Critics")) > 25);
        CHECK(mb.CastOrderRank(s, NDef("Chandra's Incinerator")) > 25);
    }
    heurarm::t_arm[heurarm::MIDRANGE_FIREBLAST_ORDER] = 0;
    CHECK(mb.CastOrderRank(s, NDef("Fireblast")) == mb.CastOrderRank(s, NDef("Lightning Bolt")));
    heurarm::t_arm[heurarm::MIDRANGE_FIREBLAST_ORDER] = -1;
}

// ---- USER 2026-11-19: the main-2 held-card clause (MTG_ONEDROPBURN_M2_HELD), the autonomous Fireblast float
// (MTG_FIREBLAST_FLOAT), Fireblast before Needle Drop / Skewer (MTG_ONEDROPBURN_FIREBLAST_EARLY) ----------------
namespace
{
struct ArmPin
{
    heurarm::Slot slot;
    ArmPin(heurarm::Slot s, bool on) : slot(s) { heurarm::t_arm[slot] = on ? 1 : 0; }
    ~ArmPin() { heurarm::t_arm[slot] = -1; }
};
// Post-combat, combat damage only, every Mountain tapped (spent in main 1); the main-2 Needle Drop has been cast
// (stamped with the given arms) and has left the hand.
GameState NHeldBoard(const std::vector<const char*>& hand, int mountains)
{
    std::vector<const char*> h = {"Needle Drop"};
    h.insert(h.end(), hand.begin(), hand.end());
    GameState s = NPostCombat(h);
    s.battlefield.resize(static_cast<std::size_t>(mountains));
    for (Permanent& p : s.battlefield) { p.tapped = true; }
    s.MarkPlayerDealtDamage(1, 2, /*combat=*/true);
    NoteMain2StagerCast(s, NDef("Needle Drop"));
    s.players[0].hand.erase(s.players[0].hand.begin());
    s.MarkPlayerDealtDamage(1, 1);
    return s;
}
}   // namespace

TEST_CASE("1 Drop Burn main-2 held-card clause: a HELD card only the Needle-drawn land pays is admitted (lever ON only)")
{
    OrderPin on(true);
    auto allowed = [&](const GameState& st, const char* name, int hi) { return OdbProv().Main2CastAllowed(st, NCastAt(name, hi)); };
    {
        ArmPin held(heurarm::ONEDROPBURN_M2_HELD, true);
        CHECK(OdbProv().Main2SnapshotsLands());
        GameState s = NHeldBoard({"Lightning Bolt"}, 3);
        CHECK_FALSE(allowed(s, "Lightning Bolt", 0));        // no new land: the old rule
        NPut(s, "Mountain", 40);                               // Needle Drop drew it; the land drop played it
        CHECK(allowed(s, "Lightning Bolt", 0));               // only the new land pays the Bolt
        GameState u = s;
        u.battlefield[0].tapped = false;                       // an OLD Mountain still pays it -> a main-1 cast
        CHECK_FALSE(allowed(u, "Lightning Bolt", 0));

        // Fireblast's alternative cost: the drawn Mountain is the SECOND Mountain.
        GameState f = NHeldBoard({"Fireblast"}, 1);
        NPut(f, "Mountain", 40);
        Action fb = NCastAt("Fireblast", 0);
        fb.tutor_target = std::string("fireblast:70,40");
        CHECK(OdbProv().Main2CastAllowed(f, fb));
        GameState g = NHeldBoard({"Fireblast"}, 2);            // two Mountains already: payable before the draw
        NPut(g, "Mountain", 40);
        fb.tutor_target = std::string("fireblast:70,71");
        CHECK_FALSE(OdbProv().Main2CastAllowed(g, fb));
    }
    ArmPin held(heurarm::ONEDROPBURN_M2_HELD, false);
    CHECK_FALSE(OdbProv().Main2SnapshotsLands());
    GameState s = NHeldBoard({"Lightning Bolt"}, 3);
    NPut(s, "Mountain", 40);
    CHECK_FALSE(allowed(s, "Lightning Bolt", 0));
}

TEST_CASE("Fireblast float: autonomous play floats {R} from each UNTAPPED Mountain it sacrifices (lever ON only)")
{
    for (int on = 0; on <= 1; ++on)
    {
        ArmPin fl(heurarm::FIREBLAST_FLOAT, on != 0);
        GameState s = NBoard(3, {});
        s.battlefield[0].tapped = true;                        // 70 tapped, 71 / 72 untapped
        REQUIRE(PayFireblastAlternative(s, 0, "fireblast:70,71"));
        CHECK(s.floating_mana.red == (on != 0 ? 1 : 0));      // only the untapped one makes mana
        CHECK(s.battlefield.size() == 1);
        CHECK(s.players[0].graveyard.size() == 2);
    }
}

TEST_CASE("1 Drop Burn order: Fireblast-early ranks Fireblast after the burn and before Needle Drop (lever ON only)")
{
    OrderPin on(true);
    GameState s = NBoard(2, {});
    s.m_provider = &OdbProv();
    {
        ArmPin early(heurarm::ONEDROPBURN_FIREBLAST_EARLY, true);
        CHECK(OdbProv().CastOrderRank(s, NDef("Fireblast")) == 28);
        CHECK(OdbProv().CastOrderRank(s, NDef("Rift Bolt")) < 28);
        CHECK(OdbProv().CastOrderRank(s, NDef("Needle Drop")) > 28);
        CHECK(OdbProv().CastOrderTierName(28) != nullptr);
    }
    ArmPin early(heurarm::ONEDROPBURN_FIREBLAST_EARLY, false);
    CHECK(OdbProv().CastOrderRank(s, NDef("Fireblast")) == 40);
}

// ---- MTG_PO_DAMAGE_ORDER: the payable-order fallback orders a damage-dependent cast after the burn that enables it --
namespace
{
Action NFireblast(int m1, int m2)
{
    Action a = NCast("Fireblast");
    a.tutor_target  = "fireblast:" + std::to_string(m1) + "," + std::to_string(m2);
    a.cost          = ManaCost{};
    a.direct_damage = 4;
    return a;
}
}   // namespace

TEST_CASE("Damage order: Needle Drop before the line's only burn moves after it, only when the given order cannot pay")
{
    // s30000108 T2 shape: two Mountains, Needle Drop ranked before Fireblast. Needle Drop has no target until
    // Fireblast resolves; Fireblast first floats both sacrificed Mountains' {R} (MTG_FIREBLAST_FLOAT).
    ArmPin fl(heurarm::FIREBLAST_FLOAT, true);
    GameState s = NBoard(2, {"Needle Drop", "Fireblast"});
    Action nd = NCast("Needle Drop"); nd.direct_damage = 1;
    const std::vector<Action> acts = {nd, NFireblast(70, 71)};
    {
        ArmPin dmg(heurarm::PO_DAMAGE_ORDER, true);
        std::vector<int> order = {0, 1};
        CHECK(TurnSolver::DamagePayableOrder(s, acts, order));
        CHECK(order == std::vector<int>{1, 0});
        std::vector<int> given = {1, 0};                       // already burn-first: kept
        CHECK_FALSE(TurnSolver::DamagePayableOrder(s, acts, given));
        CHECK(given == std::vector<int>{1, 0});
        GameState dealt = s;                                   // a target already exists: no domain, kept
        dealt.MarkPlayerDealtDamage(1, 2);
        std::vector<int> kept = {0, 1};
        CHECK_FALSE(TurnSolver::DamagePayableOrder(dealt, acts, kept));
        CHECK(kept == std::vector<int>{0, 1});
    }
    ArmPin dmg(heurarm::PO_DAMAGE_ORDER, false);               // lever off: never moves
    std::vector<int> order = {0, 1};
    CHECK_FALSE(TurnSolver::DamagePayableOrder(s, acts, order));
    CHECK(order == std::vector<int>{0, 1});
}

TEST_CASE("Damage order: a spectacle cast moves after the burn only when its printed cost cannot be paid first")
{
    ArmPin fl(heurarm::FIREBLAST_FLOAT, true);
    ArmPin dmg(heurarm::PO_DAMAGE_ORDER, true);
    const std::vector<Action> acts = {NCast("Skewer the Critics"), NFireblast(70, 71)};
    {
        // Two Mountains: Skewer's {2}{R} cannot pay first; Fireblast first floats {R}{R} and Skewer costs {R}.
        GameState s = NBoard(2, {"Skewer the Critics", "Fireblast"});
        std::vector<int> order = {0, 1};
        CHECK(TurnSolver::DamagePayableOrder(s, acts, order));
        CHECK(order == std::vector<int>{1, 0});
    }
    {
        // Four Mountains: the printed cost pays first and Fireblast then takes two tapped Mountains -- the given
        // order (the deck's) pays every cast, so it stands.
        GameState s = NBoard(4, {"Skewer the Critics", "Fireblast"});
        std::vector<int> order = {0, 1};
        CHECK_FALSE(TurnSolver::DamagePayableOrder(s, acts, order));
        CHECK(order == std::vector<int>{0, 1});
    }
}
