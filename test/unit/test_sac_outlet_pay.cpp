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

namespace
{
int OwnCreaturesSp(const GameState& s)
{
    int n = 0;
    for (const Permanent& q : s.battlefield) { if (q.controller_index == 0 && q.card.IsCreature()) { ++n; } }
    return n;
}

// Goblins s4113506 gi3505 T3 main 2: two Mountains and Three Tree City up, twelve Goblins (two Siege-Gang
// Commanders, one summoning sick with its three tokens), team at 11.
GameState Gi3505Sp()
{
    GameState s = EmptySp(11);
    PutSp(s, "Mountain", false);
    PutSp(s, "Mountain", false);
    PutSp(s, "Three Tree City", false);
    for (const char* n : { "Goblin Lackey", "Goblin Lackey", "Skirk Prospector", "Siege-Gang Commander" })
    { PutSp(s, n, /*tapped=*/true); }
    for (int t = 0; t < 3; ++t)
    { GoblinTokenSp(s); s.battlefield.back().tapped = true; s.battlefield.back().entered_this_turn = false; }
    PutSp(s, "Siege-Gang Commander", /*tapped=*/false, /*sick=*/true);
    for (int t = 0; t < 3; ++t) { GoblinTokenSp(s); }
    PutSp(s, "Stingscourger", /*tapped=*/false, /*sick=*/true);
    return s;
}

// Goblins s4154256 gi4255 T5 main 2: five Mountains up, Skirk, a Siege-Gang and three tokens tapped from
// the attack, team at 5.
GameState Gi4255Sp()
{
    GameState s = EmptySp(5);
    s.turn_number = 5;
    for (int k = 0; k < 5; ++k) { PutSp(s, "Mountain", false); }
    PutSp(s, "Skirk Prospector", /*tapped=*/true);
    PutSp(s, "Siege-Gang Commander", /*tapped=*/true);
    for (int t = 0; t < 3; ++t)
    { GoblinTokenSp(s); s.battlefield.back().tapped = true; s.battlefield.back().entered_this_turn = false; }
    return s;
}
}   // namespace

TEST_CASE("Sac outlet pay: fodder is the LAST source of the whole payment -- Three Tree City's fed mode first")
{
    // Six shots cost {6}{R}{R}{R}{R}{R}{R}. The two Mountains fed into Three Tree City make twelve {R}, so the
    // payment eats nothing; the greedy alone (which never reaches the City's fed mode -- only the backtracker
    // does, and it ran only when the greedy FAILED) paid Mountains + the City's {C} + nine Goblins, leaving
    // three victims for six shots. With the fodder-free payer first, the six shots kill (the OFF arm's T3).
    const GameState s = Gi3505Sp();
    ArmSp pay(heurarm::SAC_OUTLET_PAY, 1);
    GameState probe = s;
    ManaCost c; c.generic = 6; c.red = 6;
    REQUIRE(TapForCostDirect(probe, c, /*for_creature=*/false));
    CHECK(OwnCreaturesSp(probe) == OwnCreaturesSp(s));
    CHECK(SomePlanKillsSp(s, AllPlansSp(s)));
}

TEST_CASE("Sac outlet pay: a payment eats as FEW bodies as it can be made with")
{
    // Three shots, {3}{R}{R}{R}, off five Mountains: exactly one body is owed. The greedy's per-pip needs
    // order priced the Mountains as scarcer than the bodies and ate three tokens for the {R}s, leaving two
    // victims -- two shots, 1 life left (T6 where the OFF arm kills T5).
    const GameState s = Gi4255Sp();
    ArmSp pay(heurarm::SAC_OUTLET_PAY, 1);
    GameState probe = s;
    ManaCost c; c.generic = 3; c.red = 3;
    REQUIRE(TapForCostDirect(probe, c, /*for_creature=*/false));
    CHECK(OwnCreaturesSp(probe) == OwnCreaturesSp(s) - 1);
    int untapped_lands = 0;
    for (const Permanent& q : probe.battlefield) { if (q.card.IsLand() && !q.tapped) { ++untapped_lands; } }
    CHECK(untapped_lands == 0);
    CHECK(SomePlanKillsSp(s, AllPlansSp(s)));
}

TEST_CASE("Sac outlet pay: an ORDER of the same casts that pays without a body is preferred to one that eats")
{
    // Fungus s4245015 gi5014 (2HG) T3: Hickory Woodlot (one depletion counter), Ancient Tomb, Utopia Mycon and two
    // Saprolings, a Forest to play, Sporecrown Thallid in hand and Brightcap Badger on its adventure. Thallid
    // first spends the Forest's {G} and the Badger's {G} then has no source but Mycon eating a Saproling; Badger
    // first pays off the Woodlot + the Tomb and its grant taps Mycon and a Saproling for the Thallid. The pool
    // credits each Saproling as a mana, so Thallid-first projected payable and was kept (the Saproling and the
    // T5 kill lost, T6); the cast-order decider now looks for an order that pays from the board alone first.
    EnsureCardsSp();
    static const FungusProvider fprov;
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.players[0].life     = 20;
    s.players[1].life     = 30;
    s.players[0].lands_played_this_turn = 0;
    s.m_provider          = &fprov;
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(PhSp("Forest")); }
    PutSp(s, "Hickory Woodlot", false);
    { Counter dep; dep.type = Counter::Type::Depletion; dep.count = 1; s.battlefield.back().counters.Add(dep); }
    PutSp(s, "Ancient Tomb", false);
    PutSp(s, "Utopia Mycon", false);
    for (int k = 0; k < 2; ++k)
    {
        Permanent p;
        p.card.m_name = "1/1 Saproling Token"; p.card.RehashName(); p.card.AddColor(Color::Green);
        p.card.AddType(CardType::Creature); p.card.m_subtypes = { "Saproling" };
        p.card.m_power = 1; p.card.m_toughness = 1; p.card.m_number = 1000 + k;
        p.is_token = true; p.def_absent = true; p.controller_index = 0; p.owner_index = 0;
        s.battlefield.push_back(p);
    }
    s.players[0].hand.push_back(PhSp("Sporecrown Thallid"));
    s.players[0].hand.push_back(PhSp("Forest"));
    { Card c = PhSp("Brightcap Badger"); c.m_is_staged = true; c.m_staged_expiry = 1 << 30; s.players[0].hand.push_back(c); }
    ArmSp pay(heurarm::SAC_OUTLET_PAY, 1);
    bool seen = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlansExhaustive(s, /*is_pre_combat=*/true, nullptr, nullptr))
    {
        bool thallid = false, badger = false;
        for (const Action& a : p.actions)
        {
            if (a.card_name.str() == "Sporecrown Thallid") { thallid = true; }
            if (a.card_name.str() == "Brightcap Badger" && !a.adventure) { badger = true; }
        }
        if (!thallid || !badger || p.actions.size() != 2) { continue; }
        GameState probe = s;
        TurnSolver::ApplyPlan(probe, p, /*is_pre_combat=*/true);
        int saprolings = 0, thallids = 0, badgers = 0;
        for (const Permanent& q : probe.battlefield)
        {
            if (q.card.m_name.str() == "1/1 Saproling Token") { ++saprolings; }
            if (q.card.m_name.str() == "Sporecrown Thallid")  { ++thallids; }
            if (q.card.m_name.str() == "Brightcap Badger")    { ++badgers; }
        }
        if (thallids != 1 || badgers != 1) { continue; }   // a land variant that cannot pay both
        seen = true;
        CHECK_MESSAGE(saprolings == 2, "land=" << p.land_to_play);
    }
    CHECK(seen);
}
