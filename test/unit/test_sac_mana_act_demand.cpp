// THE SAC-MANA BURST IS SIZED FOR ACTIVATION DEMAND (MTG_SAC_MANA_ACT_DEMAND, DEFAULT ON).
//
// Skirk Prospector ("Sacrifice a Goblin: Add {R}") reaches the search as a COUNT axis whose values were
// sized for HAND spells only (the shortfall to the most expensive one it newly reaches). With an empty
// hand the outlet was worth one {R} per main phase, so a Siege-Gang Commander activation ({1}{R}, Sacrifice
// a Goblin: 2 damage) that needed more than one Skirk sacrifice was unfindable at any budget. These pin:
//   * the Goblins 2HG gi2905 T3 main-2 board (both Mountains tapped, two Siege-Gangs, fourteen Goblins,
//     opponent at 3): the menu now offers the four-sac burst, and an enumerated main-2 plan kills;
//   * with MTG_SAC_MANA_ACT_DEMAND=0 the board is back to one {R}, and no plan kills (the defect);
//   * the hand+activation landmark: Lightning Bolt + one Siege-Gang shot off three sacs;
//   * no activation sink -> no extra count (the menu is unchanged).
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <set>
#include <string>
#include <vector>

namespace
{

void EnsureCardsSd()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const GoblinsProvider& GobProvSd()
{
    static const GoblinsProvider p;
    return p;
}

int g_sd_num = 9700;

void PutSd(GameState& s, const std::string& name, bool tapped, bool sick = false)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Permanent p;
    p.card              = CardDatabase::Instance().Lookup(name)->card;
    p.card.m_number     = g_sd_num++;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.tapped            = tapped;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
}

// A 1/1 red Goblin token, built the way the --scenario loader mirrors CreateTokenOnce.
void GoblinTokenSd(GameState& s)
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

Card PhSd(const std::string& name)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = g_sd_num++;
    return c;
}

// Goblins 2HG s3212906 gi2905 (MTG_LACKEY_DECLINE=0), T3 after combat: the attackers tapped, both
// Mountains tapped for Twinshot Sniper's channel, `sgc` Siege-Gang Commanders each with its three tokens.
GameState BoardSd(int opp_life, int untapped_mountains, int sgc, const std::vector<std::string>& hand)
{
    EnsureCardsSd();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.players[0].life     = 30;
    s.players[1].life     = opp_life;
    s.players[0].lands_played_this_turn = 1;
    s.m_provider          = &GobProvSd();
    for (int k = 0; k < 2; ++k) { PutSd(s, "Mountain", /*tapped=*/k >= untapped_mountains); }
    for (const char* n : { "Goblin Lackey", "Goblin Piledriver", "Muxus, Goblin Grandee", "Goblin Lackey",
                           "Skirk Prospector", "Goblin Lackey" })
    { PutSd(s, n, /*tapped=*/true); }
    for (int g = 0; g < sgc; ++g)
    {
        PutSd(s, "Siege-Gang Commander", /*tapped=*/false, /*sick=*/true);
        for (int t = 0; t < 3; ++t) { GoblinTokenSd(s); }
    }
    for (const std::string& n : hand) { s.players[0].hand.push_back(PhSd(n)); }
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(PhSd("Mountain")); }
    return s;
}

struct ArmSd
{
    int slot; std::int8_t saved;
    ArmSd(int sl, int v) : slot(sl), saved(heurarm::t_arm[sl]) { heurarm::t_arm[sl] = static_cast<std::int8_t>(v); }
    ~ArmSd() { heurarm::t_arm[slot] = saved; }
};

// Every Skirk Prospector SacForMana count some enumerated main-2 plan takes.
std::set<int> SkirkCounts(const std::vector<TurnSolver::Plan>& plans)
{
    std::set<int> out;
    for (const TurnSolver::Plan& p : plans)
    {
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::SacForMana && a.card_name.str() == "Skirk Prospector")
            { out.insert(a.sac_count > 0 ? a.sac_count : 1); }
        }
    }
    return out;
}

// The UNCAPPED main-2 plan set (no budget -> every tranche): a completeness statement, not a menu cap.
std::vector<TurnSolver::Plan> AllPlansSd(const GameState& s)
{
    return TurnSolver::EnumerateMainPlansExhaustive(s, /*is_pre_combat=*/false, /*budget=*/nullptr,
                                                    /*truncated=*/nullptr);
}

bool SomePlanKills(const GameState& s, const std::vector<TurnSolver::Plan>& plans)
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

TEST_CASE("Sac-mana activation demand: the gi2905 main-2 board offers the four-sac burst and kills")
{
    const GameState s = BoardSd(/*opp_life=*/3, /*untapped_mountains=*/0, /*sgc=*/2, {});
    ArmSd on(heurarm::SAC_MANA_ACT_DEMAND, 1);
    const std::vector<TurnSolver::Plan> plans = AllPlansSd(s);
    const std::set<int> counts = SkirkCounts(plans);
    CHECK(counts.count(4) == 1);   // two {1}{R} shots, no land: four sacs
    CHECK(SomePlanKills(s, plans));
}

TEST_CASE("Sac-mana activation demand OFF: one {R} per main, the kill is unfindable (the defect)")
{
    const GameState s = BoardSd(3, 0, 2, {});
    ArmSd off(heurarm::SAC_MANA_ACT_DEMAND, 0);
    const std::vector<TurnSolver::Plan> plans = AllPlansSd(s);
    const std::set<int> counts = SkirkCounts(plans);
    CHECK(counts.count(4) == 0);
    CHECK_FALSE(SomePlanKills(s, plans));
}

TEST_CASE("Sac-mana activation demand: one Mountain up -> the burst is the three-sac shortfall")
{
    const GameState s = BoardSd(3, /*untapped_mountains=*/1, 1, {});
    ArmSd on(heurarm::SAC_MANA_ACT_DEMAND, 1);
    const std::vector<TurnSolver::Plan> plans = AllPlansSd(s);
    CHECK(SkirkCounts(plans).count(3) == 1);
    CHECK(SomePlanKills(s, plans));
}

TEST_CASE("Sac-mana activation demand: a VALUE outlet's one activation (Pashalik Mons, {3}{R})")
{
    // No Siege-Gang: Pashalik Mons alone ("{3}{R}, Sacrifice a Goblin: create two 1/1 Goblins"), no land up,
    // empty hand. The landmark is one activation: four sacs for {3}{R}. Off, Skirk is worth one {R}.
    GameState s = BoardSd(30, 0, /*sgc=*/0, {});
    PutSd(s, "Pashalik Mons", /*tapped=*/false);
    for (int t = 0; t < 3; ++t) { GoblinTokenSd(s); }
    {
        ArmSd on(heurarm::SAC_MANA_ACT_DEMAND, 1);
        CHECK(SkirkCounts(AllPlansSd(s)).count(4) == 1);
    }
    ArmSd off(heurarm::SAC_MANA_ACT_DEMAND, 0);
    CHECK(SkirkCounts(AllPlansSd(s)).count(4) == 0);
}

TEST_CASE("Sac-mana activation demand: a NON-lethal Siege-Gang shot is no landmark")
{
    // Team at 30, one Siege-Gang: fifteen shots are needed and the board holds ten Goblins, so there is no
    // lethal count and the chip shot is not a landmark -- the menu is the hand-landmark menu exactly.
    const GameState s = BoardSd(30, 0, 1, {});
    std::set<int> on_counts, off_counts;
    {
        ArmSd on(heurarm::SAC_MANA_ACT_DEMAND, 1);
        on_counts = SkirkCounts(AllPlansSd(s));
    }
    {
        ArmSd off(heurarm::SAC_MANA_ACT_DEMAND, 0);
        off_counts = SkirkCounts(AllPlansSd(s));
    }
    CHECK(on_counts == off_counts);
}

TEST_CASE("Sac-mana activation demand: no activation sink -> the count menu is unchanged")
{
    const GameState s = BoardSd(3, 0, /*sgc=*/0, {});
    std::set<int> on_counts, off_counts;
    {
        ArmSd on(heurarm::SAC_MANA_ACT_DEMAND, 1);
        on_counts = SkirkCounts(AllPlansSd(s));
    }
    {
        ArmSd off(heurarm::SAC_MANA_ACT_DEMAND, 0);
        off_counts = SkirkCounts(AllPlansSd(s));
    }
    CHECK(on_counts == off_counts);
}
