// Unit tests for the Prevent Damage onboarding, phase I2 (2026-09-27): the deck's five spells.
// Every number is a RULES number read off the cards' Oracle text and Scryfall rulings.
//
//  1. Spellshock: its cast trigger is a damage EVENT from a noncreature source we control ->
//     Tamanoa gains it back (and Vito drains that); lethal at 2 life even with Tamanoa out; the
//     armed subset bill counts the largest single hit, not the sum.
//  2. Pyrohemia: ONE event per activation (Dina drains once per activation); three activations kill
//     our own 1/3 Dina BEFORE the third gain resolves (so the third drains nothing); a self-lethal
//     activation is never applied; the end-step sacrifice fires only on a creature-less board
//     (an opponent spawn keeps it).
//  3. Rolling Earthquake: X = 3 kills Vito before the Tamanoa gain resolves (no drain); Tamanoa
//     triggers for the TOTAL even when the quake kills it (X = 4); lethal to both = a DRAW; X is the
//     whole 0..max range, and X = 0 is kept only under Spellshock.
//  4. Beseech the Queen: {2/B}{2/B}{2/B} parses as MV 6 / black / all generic; payable with {B}{B}{B},
//     {2}{B}{B}, ... and six generic; the enumeration emits the coloured-pip variants; the tutor
//     reaches ANY card with MV <= lands we control (one more while the land drop is open), and the
//     cap is re-read at resolution.
//  5. Green Sun's Zenith: the X axis offers Dina at X = 2 (green) but never Vito (black); the
//     resolved spell goes back into the library.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <set>
#include <string>

namespace
{

void EnsureCardsPd2()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& D(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

const PreventDamageProvider& Prov2()
{
    static const PreventDamageProvider p;
    return p;
}

GameState Board2(bool armed = true)
{
    EnsureCardsPd2();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.dmg_events_armed    = armed;
    s.own_death_live      = armed;
    s.m_provider          = &Prov2();
    return s;
}

int g_n2 = 1000;
int Put2(GameState& s, const std::string& name, int controller = 0)
{
    Permanent p;
    p.card              = D(name).card;
    p.card.m_number     = g_n2++;
    p.controller_index  = controller;
    p.owner_index       = controller;
    p.entered_this_turn = false;
    s.battlefield.push_back(p);
    return p.card.m_number;
}

void Hand2(GameState& s, const std::string& name)
{
    Card c = D(name).card;
    c.m_number = g_n2++;
    s.players[0].hand.push_back(c);
}

void Lib2(GameState& s, const std::string& name)
{
    Card c = D(name).card;
    c.m_number = g_n2++;
    s.players[0].library.push_back(c);
}

bool OnBf(const GameState& s, const std::string& name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { return true; } }
    return false;
}

int Me2(const GameState& s)  { return s.players[0].life; }
int Opp2(const GameState& s) { return s.players[1].life; }

}   // namespace

TEST_CASE("Spellshock: the cast trigger is a damage event -> Tamanoa gains it, Vito drains it")
{
    {
        GameState s = Board2();
        Put2(s, "Spellshock");
        FireOnCastTriggers(s, D("Rolling Earthquake"));
        CHECK(Me2(s) == 18);                       // no gain engine: 2 damage
    }
    {
        GameState s = Board2();
        Put2(s, "Spellshock");
        Put2(s, "Tamanoa");
        Put2(s, "Vito, Thorn of the Dusk Rose");
        FireOnCastTriggers(s, D("Beseech the Queen"));   // MV 6: no mana-value cap
        CHECK(Me2(s) == 20);                       // -2, Tamanoa +2
        CHECK(Opp2(s) == 18);                      // Vito: that much
    }
    {
        // Two Spellshocks = two events = two Tamanoa gains = two drains.
        GameState s = Board2();
        Put2(s, "Spellshock");
        Put2(s, "Spellshock");
        Put2(s, "Tamanoa");
        Put2(s, "Dina, Soul Steeper");
        FireOnCastTriggers(s, D("Green Sun's Zenith"));
        CHECK(Me2(s) == 20);
        CHECK(Opp2(s) == 18);
    }
    {
        // At 2 life the hit kills us BEFORE Tamanoa's gain resolves.
        GameState s = Board2();
        s.players[0].life = 2;
        Put2(s, "Spellshock");
        Put2(s, "Tamanoa");
        FireOnCastTriggers(s, D("Rolling Earthquake"));
        CHECK(SelfHasLost(s));
    }
    {
        // The armed subset bill: two Spellshocks at 3 life survive with Tamanoa (each 2-point hit is
        // paid back before the next), not without it; Purity makes it free; unarmed = the sum.
        GameState s = Board2();
        CHECK(dmgev::CastTriggerBill(s, 4, 2) == 4);
        Put2(s, "Tamanoa");
        CHECK(dmgev::CastTriggerBill(s, 4, 2) == 2);
        Put2(s, "Purity");
        CHECK(dmgev::CastTriggerBill(s, 4, 2) == 0);
        GameState u = Board2(/*armed=*/false);
        Put2(u, "Tamanoa");
        CHECK(dmgev::CastTriggerBill(u, 4, 2) == 4);
    }
    CHECK(D("Spellshock").params.on_cast_trigger_max_mv >= 1000);
}

TEST_CASE("Pyrohemia: one damage event per activation; three pings kill our Dina before the third gain")
{
    GameState s = Board2();
    const int pyro = Put2(s, "Pyrohemia");
    Put2(s, "Tamanoa");                          // 2/4
    Put2(s, "Dina, Soul Steeper");               // 1/3
    // Activation 1: 1 to Tamanoa, Dina, us, opp = 4 dealt -> Tamanoa +4 -> Dina drains 1.
    ApplyPermAbility(s, 0, pyro, PermAbilityMode::PingAll);
    CHECK(Me2(s) == 23);
    CHECK(Opp2(s) == 18);
    ApplyPermAbility(s, 0, pyro, PermAbilityMode::PingAll);
    CHECK(Me2(s) == 26);
    CHECK(Opp2(s) == 16);                        // Dina drained ONCE per activation, not per point
    // Activation 3: Dina takes her third damage and dies with the creature pass, so the gain (still
    // 4: two creatures + two players were dealt damage) resolves with no Dina to drain.
    ApplyPermAbility(s, 0, pyro, PermAbilityMode::PingAll);
    CHECK_FALSE(OnBf(s, "Dina, Soul Steeper"));
    CHECK(OnBf(s, "Tamanoa"));
    CHECK(Me2(s) == 29);
    CHECK(Opp2(s) == 15);                        // the ping only -- no drain
}

TEST_CASE("Pyrohemia: never applies a self-lethal ping; K is searched over its whole range")
{
    GameState s = Board2();
    s.players[0].life = 1;
    const int pyro = Put2(s, "Pyrohemia");
    Put2(s, "Tamanoa");
    CHECK_FALSE(PingAllSelfSafe(s, 0, D("Pyrohemia")));
    ApplyPermAbility(s, 0, pyro, PermAbilityMode::PingAll);
    CHECK(Me2(s) == 1);                          // not applied
    CHECK(Opp2(s) == 20);
    // Purity prevents the hit -> safe at any life.
    Put2(s, "Purity");
    CHECK(PingAllSelfSafe(s, 0, D("Pyrohemia")));

    // The generic K range is 1..affordable (the cap of 3 would steal the decision).
    const Permanent* src = nullptr;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == "Pyrohemia") { src = &p; } }
    REQUIRE(src != nullptr);
    const std::vector<int> ks = DefaultProvider().ManaSinkActivationCounts(s, *src, PermAbilityMode::PingAll, 6);
    CHECK(ks.size() == 6);
    CHECK(ks.back() == 6);
    CHECK_FALSE(PermAbilityTaps(PermAbilityMode::PingAll));
}

TEST_CASE("Pyrohemia: SpendRepeatActivations pays {R} per ping and each ping is its own event")
{
    GameState s = Board2();
    const int pyro = Put2(s, "Pyrohemia");
    Put2(s, "Tamanoa");
    Put2(s, "Dina, Soul Steeper");
    for (int k = 0; k < 2; ++k) { Put2(s, "Mountain"); }
    const int fired = SpendRepeatActivations(s, 0, pyro, PermAbilityMode::PingAll, D("Pyrohemia"), 2);
    CHECK(fired == 2);
    CHECK(Opp2(s) == 16);                        // 2 pings + 2 Dina drains
}

TEST_CASE("Pyrohemia: the end-step sacrifice needs a creature-LESS battlefield (spawns count)")
{
    {
        GameState s = Board2();
        Put2(s, "Pyrohemia");
        PerformEndStepNoCreatureSacrifice(s);
        CHECK_FALSE(OnBf(s, "Pyrohemia"));
        REQUIRE(s.players[0].graveyard.size() == 1);
        CHECK(s.players[0].graveyard.front().m_name.str() == "Pyrohemia");
    }
    {
        GameState s = Board2();
        Put2(s, "Pyrohemia");
        Put2(s, "Llanowar Elves", /*controller=*/1);   // an opponent creature keeps it
        PerformEndStepNoCreatureSacrifice(s);
        CHECK(OnBf(s, "Pyrohemia"));
    }
}

TEST_CASE("Rolling Earthquake: X = 3 kills Vito before the Tamanoa gain resolves -- no drain")
{
    GameState s = Board2();
    Put2(s, "Tamanoa");                          // 2/4
    Put2(s, "Vito, Thorn of the Dusk Rose");     // 1/3
    PerformDamageEachCreatureAndPlayer(s, 0, D("Rolling Earthquake"), 3);
    CHECK_FALSE(OnBf(s, "Vito, Thorn of the Dusk Rose"));
    CHECK(OnBf(s, "Tamanoa"));
    CHECK(Me2(s) == 20 - 3 + 12);                // one event: 3 x (2 creatures + 2 players)
    CHECK(Opp2(s) == 17);                        // the quake only: Vito died first
}

TEST_CASE("Rolling Earthquake: a Tamanoa the quake kills still triggers for the total")
{
    GameState s = Board2();
    Put2(s, "Tamanoa");
    Put2(s, "Rhox Faithmender");                 // 1/5 survives X = 4 and doubles the gain
    PerformDamageEachCreatureAndPlayer(s, 0, D("Rolling Earthquake"), 4);
    CHECK_FALSE(OnBf(s, "Tamanoa"));
    CHECK(OnBf(s, "Rhox Faithmender"));
    CHECK(Me2(s) == 20 - 4 + 2 * 16);            // 4 x (2 creatures + 2 players) = 16, doubled
    CHECK(Opp2(s) == 16);
}

TEST_CASE("Rolling Earthquake: lethal to both players is a DRAW")
{
    GameState s = Board2();
    s.players[0].life = 3;
    s.players[1].life = 3;
    Put2(s, "Tamanoa");
    PerformDamageEachCreatureAndPlayer(s, 0, D("Rolling Earthquake"), 3);
    CHECK(SelfHasLost(s));
    CHECK_FALSE(OpponentHasLost(s));
}

TEST_CASE("Rolling Earthquake: X is the whole range; X = 0 only under Spellshock")
{
    GameState s = Board2();
    const std::vector<int> gen = DefaultProvider().XCandidates(s, D("Rolling Earthquake"), 4);
    CHECK(gen == std::vector<int>{0, 1, 2, 3, 4});
    const std::vector<int> pd = Prov2().XCandidates(s, D("Rolling Earthquake"), 4);
    CHECK(pd == std::vector<int>{1, 2, 3, 4});
    Put2(s, "Spellshock");
    CHECK(Prov2().XCandidates(s, D("Rolling Earthquake"), 4) == gen);

    // The enumerator emits one cast per X (Mountains: {X}{R} with 4 lands -> X 1..3).
    GameState e = Board2();
    for (int k = 0; k < 4; ++k) { Put2(e, "Mountain"); }
    Hand2(e, "Rolling Earthquake");
    for (int k = 0; k < 10; ++k) { Lib2(e, "Mountain"); }
    auto xs_of = [](const GameState& g)
    {
        std::set<int> xs;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(g, /*is_pre_combat=*/true))
        {
            for (const Action& a : p.actions)
            { if (a.card_name.str() == "Rolling Earthquake") { xs.insert(a.chosen_x); } }
        }
        return xs;
    };
    heurarm::t_arm[heurarm::PD_QUAKE_TOP_X] = 0;
    CHECK(xs_of(e) == std::set<int>{1, 2, 3});
    heurarm::t_arm[heurarm::PD_QUAKE_TOP_X] = -1;
    CHECK(xs_of(e) == std::set<int>{3});            // shipped TOP_X: no creature of ours -> all-in
}

TEST_CASE("Beseech the Queen: {2/B} x3 is MV 6, black, all generic -- and each coloured payment")
{
    EnsureCardsPd2();
    const ManaCost& c = D("Beseech the Queen").card.m_mana_cost;
    CHECK(c.ManaValue() == 6);
    CHECK(c.generic == 6);
    CHECK(c.black == 0);
    CHECK(c.twobrid_count == 3);
    CHECK(D("Beseech the Queen").card.HasColor(Color::Black));
    for (int k = 0; k <= 3; ++k)
    {
        ManaCost v = c;
        v.PayTwobridWithColor(k);
        CHECK(v.black == k);
        CHECK(v.generic == 6 - 2 * k);
        CHECK(v.ManaValue() == 6 - k);
    }
    // Payable with {B}{B}{B} off three black sources...
    {
        GameState s = Board2(/*armed=*/false);
        for (int k = 0; k < 3; ++k) { Put2(s, "Overgrown Tomb"); }
        ManaCost v = c; v.PayTwobridWithColor(3);
        CHECK(TapForCostShared(s, v, false, nullptr, true));
    }
    // ...{2}{B}{B} off two black + two other...
    {
        GameState s = Board2(false);
        for (int k = 0; k < 2; ++k) { Put2(s, "Overgrown Tomb"); }
        for (int k = 0; k < 2; ++k) { Put2(s, "Mountain"); }
        ManaCost v = c; v.PayTwobridWithColor(2);
        CHECK(TapForCostShared(s, v, false, nullptr, true));
    }
    // ...and six generic with no black at all.
    {
        GameState s = Board2(false);
        for (int k = 0; k < 6; ++k) { Put2(s, "Mountain"); }
        CHECK(TapForCostShared(s, c, false, nullptr, true));
    }
}

TEST_CASE("Beseech the Queen: the enumeration offers every coloured-pip payment as a variant")
{
    GameState s = Board2(false);
    for (int k = 0; k < 3; ++k) { Put2(s, "Overgrown Tomb"); }
    for (int k = 0; k < 3; ++k) { Put2(s, "Mountain"); }
    Hand2(s, "Beseech the Queen");
    Lib2(s, "Tamanoa");
    for (int k = 0; k < 10; ++k) { Lib2(s, "Mountain"); }
    std::set<int> seen;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        for (const Action& a : p.actions)
        {
            if (a.card_name.str() != "Beseech the Queen") { continue; }
            seen.insert(a.twobrid_colored);
            CHECK(a.cost.ManaValue() == 6 - a.twobrid_colored);
        }
    }
    CHECK(seen == std::set<int>{0, 1, 2, 3});
}

TEST_CASE("Beseech the Queen: ANY card with MV <= lands you control; the cap is read at resolution")
{
    GameState s = Board2(false);
    Put2(s, "Mountain");
    Put2(s, "Mountain");
    Lib2(s, "Tamanoa");                          // MV 3
    Lib2(s, "Dina, Soul Steeper");               // MV 2
    Lib2(s, "Living Wish");                      // MV 2 (a sorcery: empty tutor_types = any card)
    Lib2(s, "Mountain");                         // MV 0
    const CardParams& pp = D("Beseech the Queen").params;
    // Two lands, no land in hand -> MV <= 2.
    std::vector<std::string> c = Prov2().TutorCandidates(s, 0, pp);
    CHECK(std::find(c.begin(), c.end(), "Tamanoa") == c.end());
    CHECK(std::find(c.begin(), c.end(), "Dina, Soul Steeper") != c.end());
    CHECK(std::find(c.begin(), c.end(), "Living Wish") != c.end());
    CHECK(c.back() == "Mountain");               // nonlands first
    // A land in hand with the drop open -> the plan that plays it first can reach MV 3.
    Hand2(s, "Mountain");
    c = Prov2().TutorCandidates(s, 0, pp);
    CHECK(std::find(c.begin(), c.end(), "Tamanoa") != c.end());
    // ...but at RESOLUTION on two lands a baked Tamanoa is illegal and is re-picked.
    PerformTutor(s, 0, pp, "Tamanoa", "Beseech the Queen");
    const std::vector<Card>& h = s.players[0].hand;
    CHECK(std::none_of(h.begin(), h.end(), [](const Card& x) { return x.m_name.str() == "Tamanoa"; }));
    CHECK(h.size() == 2);                        // the Mountain + a legal re-pick
}

TEST_CASE("Green Sun's Zenith: the X axis reaches Dina at X = 2 (green), never Vito; it shuffles back")
{
    GameState s = Board2(false);
    for (int k = 0; k < 3; ++k) { Put2(s, "Forest"); }
    Hand2(s, "Green Sun's Zenith");
    Lib2(s, "Dina, Soul Steeper");
    Lib2(s, "Vito, Thorn of the Dusk Rose");
    for (int k = 0; k < 10; ++k) { Lib2(s, "Mountain"); }
    const TurnSolver::Plan* dina = nullptr;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    for (const TurnSolver::Plan& p : plans)
    {
        for (const Action& a : p.actions)
        {
            if (a.card_name.str() != "Green Sun's Zenith") { continue; }
            CHECK(a.tutor_target.str() != "Vito, Thorn of the Dusk Rose");
            if (a.tutor_target.str() == "Dina, Soul Steeper")
            {
                CHECK(a.chosen_x == 2);
                if (p.actions.size() == 1) { dina = &p; }
            }
        }
    }
    REQUIRE(dina != nullptr);
    const std::size_t lib0 = s.players[0].library.size();
    GameState after = s;
    TurnSolver::ApplyPlan(after, *dina, /*is_pre_combat=*/true);
    CHECK(OnBf(after, "Dina, Soul Steeper"));
    // Dina left the library and GSZ went into it: the size is unchanged, GSZ is not in the graveyard.
    CHECK(after.players[0].library.size() == lib0);
    const std::vector<Card>& gy = after.players[0].graveyard;
    CHECK(std::none_of(gy.begin(), gy.end(), [](const Card& x) { return x.m_name.str() == "Green Sun's Zenith"; }));
    bool in_lib = false;
    for (const Card& x : after.players[0].library) { if (x.m_name.str() == "Green Sun's Zenith") { in_lib = true; } }
    CHECK(in_lib);
}

// ---- Stage 5 verification guards (2026-09-27) ---------------------------------------------------
// Found by reading real games (analysis ledger, "Stage 5 -- verification"): the search committed a
// suicide Rolling Earthquake at 1 life under Manabarbs, and burned 7 life on a second Vito that the
// legend rule killed on resolution. All three are dominance prunes derived from the rules.

TEST_CASE("Stage 5: Rolling Earthquake's X never reaches our own life; Purity lifts the cap")
{
    GameState s = Board2();
    s.players[0].life = 3;
    CHECK(Prov2().XCandidates(s, D("Rolling Earthquake"), 5) == std::vector<int>{1, 2});
    Put2(s, "Purity");
    CHECK(Prov2().XCandidates(s, D("Rolling Earthquake"), 5) == std::vector<int>{1, 2, 3, 4, 5});
}

TEST_CASE("Rolling Earthquake survival ceiling: no X below it, none above it unless lethal (USER doctrine)")
{
    GameState s = Board2();
    Put2(s, "Vito, Thorn of the Dusk Rose");         // 1/3 -> ceiling 2
    Put2(s, "Rhox Faithmender");                     // 1/5
    const CardDefinition& q = D("Rolling Earthquake");
    heurarm::t_arm[heurarm::PD_QUAKE_NO_OVERKILL] = 0;
    CHECK(Prov2().XCandidates(s, q, 6) == std::vector<int>{1, 2, 3, 4, 5, 6});   // levers off
    heurarm::t_arm[heurarm::PD_QUAKE_NO_UNDERSHOOT] = 1;
    CHECK(Prov2().XCandidates(s, q, 6) == std::vector<int>{2, 3, 4, 5, 6});
    CHECK(Prov2().XCandidates(s, q, 1) == std::vector<int>{1});                  // ceiling out of reach: all-in
    heurarm::t_arm[heurarm::PD_QUAKE_NO_OVERKILL] = 1;
    CHECK(Prov2().XCandidates(s, q, 6) == std::vector<int>{2});
    s.players[1].life = 5;                                                       // X = 5 kills them
    CHECK(Prov2().XCandidates(s, q, 6) == std::vector<int>{2, 5});
    heurarm::t_arm[heurarm::PD_QUAKE_NO_UNDERSHOOT] = 0;
    CHECK(Prov2().XCandidates(s, q, 6) == std::vector<int>{1, 2, 5});
    heurarm::t_arm[heurarm::PD_QUAKE_NO_UNDERSHOOT] = -1;
    heurarm::t_arm[heurarm::PD_QUAKE_NO_OVERKILL] = -1;
}

TEST_CASE("Rolling Earthquake two-turn lethal: max X past the ceiling when a second quake kills next turn")
{
    GameState s = Board2();
    for (int k = 0; k < 7; ++k) { Put2(s, "Mountain"); }     // 7 mana: X <= 6 now, next turn 7 + drop
    Put2(s, "Tamanoa");                                       // 2/4 -> ceiling 3
    Hand2(s, "Rolling Earthquake");
    Hand2(s, "Rolling Earthquake");
    Hand2(s, "Mountain");
    s.players[1].life = 14;                                   // 6 now + 7 next turn = 13: short
    const CardDefinition& q = D("Rolling Earthquake");   // (below the ceiling: TOP_X trims at PLAN level)
    heurarm::t_arm[heurarm::PD_QUAKE_TWO_TURN] = 1;
    CHECK(Prov2().XCandidates(s, q, 6) == std::vector<int>{1, 2, 3});
    s.players[1].life = 13;                                   // now exactly lethal over two turns
    CHECK(Prov2().XCandidates(s, q, 6) == std::vector<int>{1, 2, 3, 6});
    heurarm::t_arm[heurarm::PD_QUAKE_TWO_TURN] = 0;           // lever off: ceiling only
    CHECK(Prov2().XCandidates(s, q, 6) == std::vector<int>{1, 2, 3});
    s.players[0].hand.pop_back(); s.players[0].hand.pop_back();   // one quake, no land in hand
    heurarm::t_arm[heurarm::PD_QUAKE_TWO_TURN] = 1;
    CHECK(Prov2().XCandidates(s, q, 6) == std::vector<int>{1, 2, 3});
    heurarm::t_arm[heurarm::PD_QUAKE_TWO_TURN] = -1;
}

TEST_CASE("Rolling Earthquake TOP-X: among plans differing only in X, the largest X under the ceiling")
{
    auto xs_of = [](const GameState& e)
    {
        std::set<int> xs;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(e, /*is_pre_combat=*/true))
        {
            for (const Action& a : p.actions)
            { if (a.card_name.str() == "Rolling Earthquake") { xs.insert(a.chosen_x); } }
        }
        return xs;
    };
    GameState e = Board2();
    for (int k = 0; k < 5; ++k) { Put2(e, "Mountain"); }
    Put2(e, "Vito, Thorn of the Dusk Rose");         // 1/3 -> ceiling 2
    Hand2(e, "Rolling Earthquake");
    for (int k = 0; k < 10; ++k) { Lib2(e, "Mountain"); }
    heurarm::t_arm[heurarm::PD_QUAKE_NO_OVERKILL] = 0;
    heurarm::t_arm[heurarm::PD_QUAKE_TOP_X] = 0;
    CHECK(xs_of(e) == std::set<int>{1, 2, 3, 4});
    heurarm::t_arm[heurarm::PD_QUAKE_TOP_X] = 1;
    CHECK(xs_of(e) == std::set<int>{2, 3, 4});     // X = 1 collapses into X = 2; overkill untouched
    heurarm::t_arm[heurarm::PD_QUAKE_TOP_X] = -1;
    heurarm::t_arm[heurarm::PD_QUAKE_NO_OVERKILL] = -1;
    CHECK(xs_of(e) == std::set<int>{2});            // shipped defaults: ceiling only (opponent at 20)
}

TEST_CASE("Stage 5: the first land tap under Manabarbs kills -- per event with Tamanoa, summed without")
{
    GameState s = Board2();
    Put2(s, "Manabarbs");
    s.players[0].life = 1;
    CHECK(dmgev::FirstLandTapKills(s, 0));
    s.players[0].life = 2;
    CHECK_FALSE(dmgev::FirstLandTapKills(s, 0));
    Put2(s, "Manabarbs", /*controller=*/1);        // "whenever a PLAYER taps a land": theirs hits us too
    CHECK(dmgev::FirstLandTapKills(s, 0));         // 1 + 1 before any gain, no Tamanoa
    Put2(s, "Tamanoa");
    CHECK_FALSE(dmgev::FirstLandTapKills(s, 0));   // each 1-point barb is gained back before the next
    s.players[0].life = 1;
    CHECK(dmgev::FirstLandTapKills(s, 0));         // ...but the first one still kills at 1 (SBA first)
    Put2(s, "Purity");
    CHECK_FALSE(dmgev::FirstLandTapKills(s, 0));   // prevented
    GameState u = Board2(/*armed=*/false);
    Put2(u, "Manabarbs");
    u.players[0].life = 1;
    CHECK_FALSE(dmgev::FirstLandTapKills(u, 0));   // unarmed board: the legacy model, never asked
}

TEST_CASE("Stage 5: at 1 life under Manabarbs the enumerator offers no cast at all")
{
    GameState e = Board2();
    for (int k = 0; k < 4; ++k) { Put2(e, "Mountain"); }
    Put2(e, "Manabarbs");
    Hand2(e, "Spellshock");
    for (int k = 0; k < 10; ++k) { Lib2(e, "Mountain"); }
    auto casts = [](const GameState& g)
    {
        int n = 0;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(g, /*is_pre_combat=*/true))
        { n += static_cast<int>(p.actions.size()); }
        return n;
    };
    e.players[0].life = 5;
    CHECK(casts(e) > 0);
    e.players[0].life = 1;
    CHECK(casts(e) == 0);
}

TEST_CASE("Stage 5: a duplicate Vito is offered only when its CAST can pay (Spellshock + a gain engine)")
{
    GameState s = Board2();
    const CardDefinition& vito = D("Vito, Thorn of the Dusk Rose");
    CHECK(Prov2().OfferDuplicateLegendCast(s, 0, vito));           // no copy out: an ordinary cast
    Put2(s, "Vito, Thorn of the Dusk Rose");
    CHECK_FALSE(Prov2().OfferDuplicateLegendCast(s, 0, vito));     // dies to the legend rule, for nothing
    Put2(s, "Spellshock");
    CHECK_FALSE(Prov2().OfferDuplicateLegendCast(s, 0, vito));     // its trigger only hurts us
    Hand2(s, "Tamanoa");
    CHECK_FALSE(Prov2().OfferDuplicateLegendCast(s, 0, vito));     // engine in hand, no mana for both
    for (int k = 0; k < 7; ++k) { Put2(s, "Mountain"); }
    CHECK(Prov2().OfferDuplicateLegendCast(s, 0, vito));           // Tamanoa then the duplicate: affordable
    GameState t = Board2();
    Put2(t, "Vito, Thorn of the Dusk Rose");
    Put2(t, "Spellshock");
    Put2(t, "Tamanoa");
    CHECK(Prov2().OfferDuplicateLegendCast(t, 0, vito));           // the trigger becomes a drain
}
