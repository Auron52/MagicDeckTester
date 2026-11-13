// Boars I2b (2026-11-13): Herald of Ilharg -- "Whenever you cast a creature spell, put two +1/+1 counters on this
// creature. If that spell has mana value 5 or greater, this creature deals damage equal to the number of counters
// on it to each opponent." (creature_cast_trigger_plus_counters / creature_cast_counter_damage_min_mv, fired from
// the shared FireOnCastTriggers), its this-turn projection term (MTG_ATTACK_HERALD_TRIGGER), and the REQUIRED
// `mana_spent` argument FireOnCastTriggers now carries for Raggadragga's later "if at least seven mana was spent
// to cast it" (checked through the probe at both worlds' hand-cast sites, with and without a Herald's Horn).
#include <doctest/doctest.h>
#include "ai/AIEngine.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"
#include <functional>
#include <string>
#include <vector>

namespace
{
const CardDefinition& HDef(const char* name)
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
Card HCard(const char* name, int number)
{
    Card c = HDef(name).card;
    c.m_number = number;
    return c;
}
Permanent& HPut(GameState& s, const char* name, int number, bool sick = false)
{
    Permanent p;
    p.card = HCard(name, number);
    p.controller_index = p.owner_index = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}
GameState HBase(int opp_life = 20)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = opp_life;
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(HCard("Forest", 900 + k)); }
    return s;
}
const Permanent* HNum(const GameState& s, int num)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == num) { return &p; } }
    return nullptr;
}
int Counters(const GameState& s, int num) { const Permanent* p = HNum(s, num); return p ? TotalCountersOn(*p) : -1; }
Action HCast(const char* name)
{
    Action a;
    a.kind           = Action::Kind::CastFromHand;
    a.card_name      = name;
    a.def            = &HDef(name);
    a.hand_index     = -1;
    a.cost           = HDef(name).card.m_mana_cost;
    a.is_noncreature = !HDef(name).card.IsCreature();
    return a;
}
TurnSolver::Plan HPlan(const std::vector<Action>& acts) { TurnSolver::Plan p; p.actions = acts; return p; }
void HLands(GameState& s, int forests, int mountains)
{
    for (int k = 0; k < forests; ++k) { HPut(s, "Forest", 50 + k); }
    for (int k = 0; k < mountains; ++k) { HPut(s, "Mountain", 70 + k); }
}
// Herald's Horn on the battlefield, Boar already named (the I1 choice; set directly here).
void HHorn(GameState& s, int number)
{
    Permanent& h = HPut(s, "Herald's Horn", number);
    h.chosen_subtype_id = SubtypeRegistry::Instance().Id("Boar");
}
}   // namespace

TEST_CASE("Herald of Ilharg: card data")
{
    const CardDefinition& d = HDef("Herald of Ilharg");
    CHECK(d.tmpl == CardTemplate::VanillaCreature);
    CHECK(d.params.creature_cast_trigger_plus_counters == 2);
    CHECK(d.params.creature_cast_counter_damage_min_mv == 5);
    CHECK(d.card.m_mana_cost.ManaValue() == 4);
    CHECK(d.card.m_power == 3);
    CHECK(d.card.m_toughness == 3);
    CHECK(d.card.HasKeyword(Keyword::Trample));
}

TEST_CASE("Herald of Ilharg: the cast trigger -- counters, the MV >= 5 damage, several Heralds, its own cast")
{
    SUBCASE("an MV-3 creature: +2 counters, no damage; a noncreature spell: nothing")
    {
        GameState s = HBase();
        HPut(s, "Herald of Ilharg", 10);
        FireOnCastTriggers(s, HDef("Reclamation Sage"), /*mana_spent=*/3);
        CHECK(Counters(s, 10) == 2);
        CHECK(s.players[1].life == 20);
        FireOnCastTriggers(s, HDef("Lightning Bolt"), /*mana_spent=*/1);
        CHECK(Counters(s, 10) == 2);
        CHECK(s.players[1].life == 20);
    }
    SUBCASE("an MV-9 creature on a Herald with 2 counters: 4 counters, then 4 damage (counted AFTER the put)")
    {
        GameState s = HBase();
        HPut(s, "Herald of Ilharg", 10);
        FireOnCastTriggers(s, HDef("Reclamation Sage"), 3);
        FireOnCastTriggers(s, HDef("Apex Altisaur"), 9);
        CHECK(Counters(s, 10) == 4);
        CHECK(s.players[1].life == 16);
        CHECK(s.opponent_lost_life_this_turn);
    }
    SUBCASE("two Heralds: each deals its OWN count")
    {
        GameState s = HBase();
        HPut(s, "Herald of Ilharg", 10);
        HPut(s, "Herald of Ilharg", 11);
        FireOnCastTriggers(s, HDef("Reclamation Sage"), 3);    // 10: 2, 11: 2
        // Only #10 survives a hypothetical second cast window: give #11 two more by hand -> 4.
        AddPlusCounters(s.battlefield[1], 2);
        FireOnCastTriggers(s, HDef("Apex Altisaur"), 9);       // 10: 4 -> 4 dmg, 11: 6 -> 6 dmg
        CHECK(Counters(s, 10) == 4);
        CHECK(Counters(s, 11) == 6);
        CHECK(s.players[1].life == 10);
    }
    SUBCASE("casting a Herald: the one already out takes +2 (MV 4, no damage); the new one is not on the battlefield")
    {
        GameState s = HBase();
        HPut(s, "Herald of Ilharg", 10);
        FireOnCastTriggers(s, HDef("Herald of Ilharg"), 4);
        CHECK(Counters(s, 10) == 2);
        CHECK(s.players[1].life == 20);
    }
    SUBCASE("the opponent's Herald does not trigger on OUR cast")
    {
        GameState s = HBase();
        Permanent& theirs = HPut(s, "Herald of Ilharg", 10);
        theirs.controller_index = theirs.owner_index = 1;
        FireOnCastTriggers(s, HDef("Apex Altisaur"), 9);
        CHECK(Counters(s, 10) == 0);
        CHECK(s.players[1].life == 20);
    }
    SUBCASE("2HG: the damage is dealt to EACH opponent -- twice off the shared team total")
    {
        const gamesetup::Setup saved = gamesetup::t_setup;
        gamesetup::t_setup.opponent_heads = 2;
        REQUIRE(gamesetup::OpponentHeads() == 2);
        GameState s = HBase(30);
        HPut(s, "Herald of Ilharg", 10);
        FireOnCastTriggers(s, HDef("Apex Altisaur"), 9);
        CHECK(Counters(s, 10) == 2);
        CHECK(s.players[1].life == 26);
        gamesetup::t_setup = saved;
    }
}

TEST_CASE("Herald of Ilharg: executor and rollout realise the same trigger from a real hand cast")
{
    auto board = [] {
        GameState s = HBase();
        HPut(s, "Herald of Ilharg", 10);
        HLands(s, 9, 0);
        s.players[0].hand.push_back(HCard("Apex Altisaur", 60));
        return s;
    };
    GameState roll = board();
    TurnSolver::ApplyPlan(roll, HPlan({ HCast("Apex Altisaur") }), /*is_pre_combat=*/true);
    CHECK(Counters(roll, 10) == 2);
    CHECK(roll.players[1].life == 18);

    GameState exec = board();
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    ManaPool avail = AvailableManaPool(exec);
    MtgTestSeam::CastSpellFromHand(eng, exec, exec.players[0].hand.back(), avail, 0);
    CHECK(Counters(exec, 10) == 2);
    CHECK(exec.players[1].life == 18);
}

TEST_CASE("FireOnCastTriggers(mana_spent): a hard cast reports the FINAL paid cost in both worlds, Horn or not")
{
    // Herald of Ilharg ({2}{R}{G}) is a Boar: a Herald's Horn naming Boar makes it {1}{R}{G}.
    for (const bool horn : { false, true })
    {
        CAPTURE(horn);
        auto board = [horn] {
            GameState s = HBase();
            HLands(s, 3, 2);
            if (horn) { HHorn(s, 20); }
            s.players[0].hand.push_back(HCard("Herald of Ilharg", 60));
            return s;
        };
        const int want = horn ? 3 : 4;
        {
            const GameState s = board();
            CHECK(EffectiveSpellCost(HDef("Herald of Ilharg"), s, 1).ManaValue() == want);
        }
        g_cast_trigger_mana_spent_probe = -1;
        GameState roll = board();
        TurnSolver::ApplyPlan(roll, HPlan({ HCast("Herald of Ilharg") }), /*is_pre_combat=*/true);
        CHECK(g_cast_trigger_mana_spent_probe == want);

        g_cast_trigger_mana_spent_probe = -1;
        GameState exec = board();
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        ManaPool avail = AvailableManaPool(exec);
        MtgTestSeam::CastSpellFromHand(eng, exec, exec.players[0].hand.back(), avail, 0);
        CHECK(g_cast_trigger_mana_spent_probe == want);
        CHECK(exec.stack.size() == 1);
    }
}

namespace
{
// The this-turn projection, read the way test_attack_projection.cpp reads Mana Cannons': the largest opponent life
// at which a plan casting exactly `cast` (and nothing else) still reads `wins_this_turn`.
int HThreshold(const std::function<GameState(int)>& board, const char* cast)
{
    int best = 0;
    bool seen = false;
    for (int life = 1; life <= 40; ++life)
    {
        const GameState s = board(life);
        bool win = false;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
        {
            int casts = 0; bool it = false;
            for (const Action& a : p.actions)
            {
                if (a.kind == Action::Kind::PlayLand) { continue; }
                ++casts;
                if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == cast) { it = true; }
            }
            if (casts != 1 || !it) { continue; }
            seen = true;
            if (p.wins_this_turn) { win = true; }
        }
        if (win) { best = life; }
    }
    return seen ? best : -1;
}
GameState HProjBoard(int opp_life)
{
    GameState s = HBase(opp_life);
    HPut(s, "Herald of Ilharg", 10);   // 3/3, not sick: attacks this turn
    HLands(s, 9, 0);
    s.players[0].hand.push_back(HCard("Apex Altisaur", 60));
    return s;
}
}   // namespace

TEST_CASE("MTG_ATTACK_HERALD_TRIGGER: the this-turn projection credits the ping and the attacking Herald's growth")
{
    heurarm::Clear();
    // Herald swings for 3 + the 2 counters Apex's cast puts on it, and the MV-9 cast pings for those 2: 7.
    const int on = HThreshold(HProjBoard, "Apex Altisaur");
    heurarm::t_arm[heurarm::ATTACK_HERALD_TRIGGER] = 0;
    const int off = HThreshold(HProjBoard, "Apex Altisaur");
    heurarm::Clear();
    CHECK(on == off + 4);
    CHECK(on >= 7);
}
