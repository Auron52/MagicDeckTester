// §2b -- A REPEATABLE SAC-FOR-MANA OUTLET AS A PAYMENT SOURCE (MTG_SAC_OUTLET_PAY).
//
// With the lever on, Skirk Prospector's "Sacrifice a Goblin: Add {R}" is not a searched COUNT axis: each
// legal Goblin is a last-ranked {R} source the payer eats exactly as often as a cost needs. These pin:
//   * the Goblins 2HG gi2905 T3 main-2 board: two Siege-Gang shots off four sacrifices kill with the
//     activation-demand landmark OFF (MTG_SAC_MANA_ACT_DEMAND=0) -- the payment model subsumes it -- and
//     the shots' payment never eats the Siege-Gang or the Goblin it is about to sacrifice;
//   * a count no landmark expresses (three Lightning Bolts off three sacrifices, the hand landmark being
//     ONE sac) is unfindable with the lever off and found with it on;
//   * bodies the SAME plan creates are fodder: Krenko's tokens pay for a Siege-Gang shot the plan-start
//     board could not (the user's load-bearing requirement, "count goblins added to the battlefield during
//     the plan"), and Fungus' same-line spore Saproling still pays for a cast;
//   * where a death PAYS (Pashalik Mons, Rundvelt Hordemaster, Mogg War Marshal) the searched action stays
//     on the menu beside the payer -- a sacrifice for the death alone must stay findable -- and where it
//     does not, the searched action is gone.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsSp()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const GoblinsProvider& GobProvSp()
{
    static const GoblinsProvider p;
    return p;
}

int g_sp_num = 9850;

void PutSp(GameState& s, const std::string& name, bool tapped, bool sick = false)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Permanent p;
    p.card              = CardDatabase::Instance().Lookup(name)->card;
    p.card.m_number     = g_sp_num++;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.tapped            = tapped;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
}

void GoblinTokenSp(GameState& s)
{
    Permanent p;
    p.card.m_name = "1/1 Goblin Token";
    p.card.RehashName();
    p.card.AddType(CardType::Creature);
    p.card.m_subtypes  = { "Goblin" };
    p.card.m_power     = 1;
    p.card.m_toughness = 1;
    p.card.m_number    = s.next_token_number++;
    p.card.AddColor(Color::Red);
    p.is_token          = true;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = true;
    s.battlefield.push_back(p);
}

Card PhSp(const std::string& name)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = g_sp_num++;
    return c;
}

GameState EmptySp(int opp_life)
{
    EnsureCardsSp();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.players[0].life     = 20;
    s.players[1].life     = opp_life;
    s.players[0].lands_played_this_turn = 1;
    s.m_provider          = &GobProvSp();
    s.deck_has_sac_mana_outlet   = true;
    s.deck_has_endstep_reanimate = false;
    s.deck_has_upkeep_reanimate  = false;
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(PhSp("Mountain")); }
    return s;
}

// The gi2905 T3 main-2 board (see test_sac_mana_act_demand.cpp): both Mountains tapped, the attackers
// tapped, two Siege-Gang Commanders each with three tokens, team at 3.
GameState Gi2905Sp()
{
    GameState s = EmptySp(3);
    for (int k = 0; k < 2; ++k) { PutSp(s, "Mountain", /*tapped=*/true); }
    for (const char* n : { "Goblin Lackey", "Goblin Piledriver", "Muxus, Goblin Grandee", "Goblin Lackey",
                           "Skirk Prospector", "Goblin Lackey" })
    { PutSp(s, n, /*tapped=*/true); }
    for (int g = 0; g < 2; ++g)
    {
        PutSp(s, "Siege-Gang Commander", /*tapped=*/false, /*sick=*/true);
        for (int t = 0; t < 3; ++t) { GoblinTokenSp(s); }
    }
    return s;
}

struct ArmSp
{
    int slot; std::int8_t saved;
    ArmSp(int sl, int v) : slot(sl), saved(heurarm::t_arm[sl]) { heurarm::t_arm[sl] = static_cast<std::int8_t>(v); }
    ~ArmSp() { heurarm::t_arm[slot] = saved; }
};

std::vector<TurnSolver::Plan> AllPlansSp(const GameState& s)
{
    return TurnSolver::EnumerateMainPlansExhaustive(s, /*is_pre_combat=*/false, /*budget=*/nullptr,
                                                    /*truncated=*/nullptr);
}

bool SkirkSearched(const std::vector<TurnSolver::Plan>& plans)
{
    for (const TurnSolver::Plan& p : plans)
    { for (const Action& a : p.actions)
      { if (a.kind == Action::Kind::SacForMana && a.card_name.str() == "Skirk Prospector") { return true; } } }
    return false;
}

bool SomePlanKillsSp(const GameState& s, const std::vector<TurnSolver::Plan>& plans)
{
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.empty()) { continue; }
        GameState probe = s;
        TurnSolver::ApplyPlan(probe, p, /*is_pre_combat=*/false);
        if (probe.ActivePlayer().life > 0 && OpponentHasLost(probe)) { return true; }
    }
    return false;
}

}   // namespace

TEST_CASE("Sac outlet pay: gi2905's two Siege-Gang shots off four sacrifices, no activation landmark")
{
    const GameState s = Gi2905Sp();
    ArmSp pay(heurarm::SAC_OUTLET_PAY, 1);
    ArmSp nad(heurarm::SAC_MANA_ACT_DEMAND, 0);
    const std::vector<TurnSolver::Plan> plans = AllPlansSp(s);
    CHECK_FALSE(SkirkSearched(plans));   // no death payoff on this board: the count axis is retired
    CHECK(SomePlanKillsSp(s, plans));
}

TEST_CASE("Sac outlet pay: OFF with no activation landmark, the gi2905 kill is unfindable (control)")
{
    const GameState s = Gi2905Sp();
    ArmSp pay(heurarm::SAC_OUTLET_PAY, 0);
    ArmSp nad(heurarm::SAC_MANA_ACT_DEMAND, 0);
    CHECK_FALSE(SomePlanKillsSp(s, AllPlansSp(s)));
}

TEST_CASE("Sac outlet pay: an activation's payment never eats its source or its victim")
{
    // One Siege-Gang, NO other Goblin but Skirk and one token: {1}{R} needs two eaten bodies and the shot a
    // third, so the only legal payment eats Skirk and the token -- never the Siege-Gang (the source) and never
    // the token the shot sacrifices. With three bodies there is exactly one shot, which is what is checked.
    GameState s = EmptySp(20);
    PutSp(s, "Skirk Prospector", /*tapped=*/false);
    PutSp(s, "Siege-Gang Commander", /*tapped=*/false, /*sick=*/true);
    const int sgc = s.battlefield.back().card.m_number;
    GoblinTokenSp(s);
    GoblinTokenSp(s);
    ArmSp pay(heurarm::SAC_OUTLET_PAY, 1);
    {
        const PaySacSpareScope spare(sgc, s.battlefield.back().card.m_number);
        GameState probe = s;
        ManaCost c; c.generic = 1; c.red = 1;
        REQUIRE(TapForCostDirect(probe, c, /*for_creature=*/false));
        bool sgc_left = false, victim_left = false;
        for (const Permanent& p : probe.battlefield)
        {
            if (p.card.m_number == sgc) { sgc_left = true; }
            if (p.card.m_number == s.battlefield.back().card.m_number) { victim_left = true; }
        }
        CHECK(sgc_left);
        CHECK(victim_left);
    }
}

TEST_CASE("Sac outlet pay: three Lightning Bolts off three sacrifices -- a count no landmark offers")
{
    // No land. The hand landmark is the shortfall to the costliest spell (one Bolt: ONE sac), so the searched
    // axis offers {0, 1}; three Bolts need three. The payer eats three of the five Goblins.
    GameState s = EmptySp(9);
    PutSp(s, "Skirk Prospector", /*tapped=*/true);
    for (int t = 0; t < 4; ++t) { GoblinTokenSp(s); }
    for (int b = 0; b < 3; ++b) { s.players[0].hand.push_back(PhSp("Lightning Bolt")); }
    {
        ArmSp pay(heurarm::SAC_OUTLET_PAY, 0);
        CHECK_FALSE(SomePlanKillsSp(s, AllPlansSp(s)));   // the defect
    }
    ArmSp pay(heurarm::SAC_OUTLET_PAY, 1);
    CHECK(SomePlanKillsSp(s, AllPlansSp(s)));
}

TEST_CASE("Sac outlet pay: Krenko's tokens, made earlier in the SAME plan, fund a Siege-Gang shot")
{
    // No land; Krenko, Skirk and a summoning-sick Siege-Gang are the only Goblins, team at 2. The shot needs
    // {1}{R} (two eaten bodies) plus a third body to sacrifice, and the Siege-Gang may be neither -- so the
    // plan-start board (Krenko, Skirk) is one body short. Krenko taps after the main casts and before the
    // trailing activations, making three tokens: the payer eats two of them, the shot sacrifices a third.
    GameState s = EmptySp(2);
    PutSp(s, "Krenko, Mob Boss", /*tapped=*/false);
    PutSp(s, "Skirk Prospector", /*tapped=*/false);
    PutSp(s, "Siege-Gang Commander", /*tapped=*/false, /*sick=*/true);
    ArmSp pay(heurarm::SAC_OUTLET_PAY, 1);
    CHECK(SomePlanKillsSp(s, AllPlansSp(s)));
}

TEST_CASE("Sac outlet pay: a death that PAYS keeps the searched action; none drops it")
{
    GameState s = EmptySp(20);
    PutSp(s, "Skirk Prospector", /*tapped=*/false);
    for (int t = 0; t < 3; ++t) { GoblinTokenSp(s); }
    ArmSp pay(heurarm::SAC_OUTLET_PAY, 1);
    const CardParams& op = CardDatabase::Instance().Lookup("Skirk Prospector")->params;
    CHECK_FALSE(SacOutletEatHasValue(s, 0, op));
    for (const char* payoff : { "Pashalik Mons", "Rundvelt Hordemaster", "Mogg War Marshal" })
    {
        GameState v = s;
        PutSp(v, payoff, /*tapped=*/false, /*sick=*/true);
        CHECK_MESSAGE(SacOutletEatHasValue(v, 0, op), payoff);
    }
    // Pashalik on board: a sacrifice for the ping alone (mana unused) must stay findable.
    GameState v = s;
    PutSp(v, "Pashalik Mons", /*tapped=*/true);
    CHECK(SkirkSearched(AllPlansSp(v)));
}

TEST_CASE("Sac outlet pay: Fungus same-line fodder -- pop a Saproling, eat it, cast Sporesower")
{
    // test/scenarios/fungus_sac_fodder_same_line_GAP.json, autonomously: three Forests, Thallid Shell-Dweller
    // holding exactly three spore counters, Utopia Mycon, Sporesower Thallid ({2}{G}{G}) in hand. The fourth
    // mana is a Saproling the plan itself makes; the payer eats it. The design doc's documented capability
    // loss when the searched outlet was suppressed -- it must be findable with the lever on.
    EnsureCardsSp();
    static const FungusProvider fprov;
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    s.m_provider          = &fprov;
    s.deck_has_sac_mana_outlet   = true;
    s.deck_has_endstep_reanimate = false;
    s.deck_has_upkeep_reanimate  = false;
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(PhSp("Forest")); }
    for (int k = 0; k < 3; ++k) { PutSp(s, "Forest", /*tapped=*/false); }
    PutSp(s, "Thallid Shell-Dweller", /*tapped=*/false);
    s.battlefield.back().spore_counters = 3;
    PutSp(s, "Utopia Mycon", /*tapped=*/false);
    s.players[0].hand.push_back(PhSp("Sporesower Thallid"));
    ArmSp pay(heurarm::SAC_OUTLET_PAY, 1);
    bool cast = false;
    for (const TurnSolver::Plan& p : AllPlansSp(s))
    {
        GameState probe = s;
        TurnSolver::ApplyPlan(probe, p, /*is_pre_combat=*/false);
        for (const Permanent& q : probe.battlefield)
        { if (q.card.m_name.str() == "Sporesower Thallid") { cast = true; } }
        if (cast) { break; }
    }
    CHECK(cast);
}


TEST_CASE("Sac outlet pay: where a death pays, the payer is OFF and the board plays as the OFF arm")
{
    // The searched action stays there (a sacrifice for the death alone must stay findable), and the payer
    // beside it would credit one body twice -- as the searched action's float AND as pool fodder.
    GameState s = EmptySp(20);
    PutSp(s, "Skirk Prospector", /*tapped=*/false);
    for (int t = 0; t < 3; ++t) { GoblinTokenSp(s); }
    ArmSp pay(heurarm::SAC_OUTLET_PAY, 1);
    CHECK(LiveSacPayOutlet(s, 0).valid());
    for (const char* payoff : { "Pashalik Mons", "Rundvelt Hordemaster", "Mogg War Marshal" })
    {
        GameState v = s;
        PutSp(v, payoff, /*tapped=*/false);
        CHECK_MESSAGE(!LiveSacPayOutlet(v, 0).valid(), payoff);
        GameState h = s;
        h.players[0].hand.push_back(PhSp(payoff));   // in hand too: the answer cannot flip mid-line
        CHECK_MESSAGE(!LiveSacPayOutlet(h, 0).valid(), payoff);
    }
}
