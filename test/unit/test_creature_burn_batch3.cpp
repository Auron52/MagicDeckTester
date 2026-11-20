// Creature Burn Stage 2, batch 3 (2026-11-20):
//   * Teetering Peaks -- "When this land enters, target creature gets +2/+0 until end of turn" (CardParams::
//     etb_target_creature_pump_power; ResolveLandEtbTargetPump at BOTH land-entry sites): the mandatory target,
//     CR 603.3d's removal with no creature, the sound folds (identity + the inert class) and the searched rank
//     (Plan::landfall_target_choice on the deck_has_land_etb_target stamp);
//   * the land POSITION after the first haste-creature cast (Plan::land_slot 1 counted by TurnSolver::LandSlotCounts):
//     "Ball Lightning, then Peaks" -- unfindable before (every plan played the land first);
//   * CreatureBurnProvider's order + plain-attacker prune (MTG_CB_PEAKS_PLAIN_FOLD, default ON).
#include <doctest/doctest.h>
#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include <algorithm>
#include <string>
#include <vector>

namespace
{
const CardDefinition& Cb3Def(const char* name)
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
Card Cb3Card(const char* name, int number)
{
    Card c = Cb3Def(name).card;
    c.m_number = number;
    return c;
}
Permanent& Cb3Put(GameState& s, const char* name, int number, bool sick = false, int controller = 0)
{
    Permanent p;
    p.card = Cb3Card(name, number);
    p.controller_index = p.owner_index = controller;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}
GameState Cb3Board(int mountains, const std::vector<const char*>& hand)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 0;
    s.deck_has_tap_ping        = true;   // StampDeckTraits' stamps for the Creature Burn list
    s.deck_has_land_etb_target = true;
    s.deck_has_landfall        = false;
    s.deck_has_scry_watcher    = false;
    for (int k = 0; k < mountains; ++k) { Cb3Put(s, "Mountain", 70 + k); }
    int n = 1;
    for (const char* h : hand) { s.players[0].hand.push_back(Cb3Card(h, n++)); }
    for (int k = 0; k < 8; ++k) { s.players[0].library.push_back(Cb3Card("Mountain", 40 + k)); }
    return s;
}
const Permanent* Cb3Find(const GameState& s, const char* name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name == name) { return &p; } }
    return nullptr;
}
bool Cb3Casts(const TurnSolver::Plan& p, const char* name)
{
    for (const Action& a : p.actions) { if (a.card_name.str() == name) { return true; } }
    return false;
}
std::vector<std::string> Cb3RepNames(const GameState& s)
{
    std::vector<int> legal, reps;
    LandEtbPumpReps(s, 0, legal, reps);
    std::vector<std::string> out;
    for (int bi : reps) { out.push_back(s.battlefield[static_cast<std::size_t>(bi)].card.m_name.str()); }
    return out;
}
}   // namespace

TEST_CASE("Teetering Peaks: parsed as a tapped red land with a +2/+0 targeted enter pump")
{
    const CardDefinition& d = Cb3Def("Teetering Peaks");
    CHECK(d.card.IsLand());
    CHECK(d.params.enters_tapped);
    CHECK(d.params.etb_target_creature_pump_power == 2);
    CHECK(d.params.etb_target_creature_pump_toughness == 0);
    CHECK(TurnSolver::LandCarriesEtbPump("Teetering Peaks"));
    CHECK_FALSE(TurnSolver::LandCarriesEtbPump("Mountain"));
}

TEST_CASE("Teetering Peaks: 3 Mountains + Ball Lightning -> 'Ball Lightning, then Peaks' is enumerated and swings for 8")
{
    GameState s = Cb3Board(3, { "Ball Lightning", "Teetering Peaks" });
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* after = nullptr;
    const TurnSolver::Plan* first = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.land_to_play != "Teetering Peaks" || !Cb3Casts(p, "Ball Lightning")) { continue; }
        if (p.land_slot == 1 && after == nullptr) { after = &p; }
        if (p.land_slot == 0 && first == nullptr) { first = &p; }
    }
    REQUIRE(after != nullptr);   // the position axis (LandSlotCounts: a haste creature cast counts)
    REQUIRE(first != nullptr);   // the land-first plan stays enumerated beside it

    GameState t = s;
    TurnSolver::ApplyPlan(t, *after, true);
    const Permanent* bl = Cb3Find(t, "Ball Lightning");
    REQUIRE(bl != nullptr);
    CHECK(bl->temp_power_bonus == 2);
    const Permanent* pk = Cb3Find(t, "Teetering Peaks");
    REQUIRE(pk != nullptr);
    CHECK(pk->tapped);
    CHECK(TurnSolver::ProjectedAttackDamageForTest(t) == 8);   // opponent 20 -> 12

    GameState u = s;
    TurnSolver::ApplyPlan(u, *first, true);   // land first: no creature yet -> the trigger is removed
    const Permanent* bl0 = Cb3Find(u, "Ball Lightning");
    REQUIRE(bl0 != nullptr);
    CHECK(bl0->temp_power_bonus == 0);
    CHECK(TurnSolver::ProjectedAttackDamageForTest(u) == 6);

    // Without the deck stamp the position is not offered (every other deck: byte-identical enumeration).
    GameState v = s;
    v.deck_has_land_etb_target = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(v, true))
    { CHECK_FALSE((p.land_to_play == "Teetering Peaks" && p.land_slot == 1)); }
}

TEST_CASE("Teetering Peaks: LandSlotCounts counts a haste creature cast only for a pump land on a stamped deck")
{
    GameState s = Cb3Board(0, {});
    Action bl;
    bl.kind = Action::Kind::CastFromHand;
    bl.card_name = "Ball Lightning";
    bl.def = &Cb3Def("Ball Lightning");
    Action fd = bl;
    fd.card_name = "Figure of Destiny";   // no haste
    fd.def = &Cb3Def("Figure of Destiny");
    Action lb = bl;
    lb.card_name = "Lightning Bolt";      // not a creature
    lb.def = &Cb3Def("Lightning Bolt");
    CHECK(TurnSolver::LandSlotCounts(s, bl, "Teetering Peaks"));
    CHECK_FALSE(TurnSolver::LandSlotCounts(s, fd, "Teetering Peaks"));
    CHECK_FALSE(TurnSolver::LandSlotCounts(s, lb, "Teetering Peaks"));
    CHECK_FALSE(TurnSolver::LandSlotCounts(s, bl, "Mountain"));
    s.deck_has_land_etb_target = false;
    CHECK_FALSE(TurnSolver::LandSlotCounts(s, bl, "Teetering Peaks"));
}

TEST_CASE("Teetering Peaks: a non-sick Goblin Fireslinger takes the +2 (attacks for 3 instead of pinging 1)")
{
    GameState s = Cb3Board(2, { "Teetering Peaks" });
    Cb3Put(s, "Goblin Fireslinger", 20);
    TurnSolver::Plan p;
    p.land_decided = true;
    p.land_to_play = "Teetering Peaks";
    TurnSolver::ApplyPlan(s, p, true);
    const Permanent* f = Cb3Find(s, "Goblin Fireslinger");
    REQUIRE(f != nullptr);
    CHECK(f->temp_power_bonus == 2);
    CHECK(TurnSolver::ProjectedAttackDamageForTest(s) == 3);   // +2 over the 1-damage ping it would otherwise give
}

TEST_CASE("Teetering Peaks: only a summoning-sick Cinder Pyromancer -> the trigger lands on it, no extra damage")
{
    GameState s = Cb3Board(2, { "Teetering Peaks" });
    Cb3Put(s, "Cinder Pyromancer", 21, /*sick=*/true);
    CHECK(Cb3RepNames(s).size() == 1);   // the inert class alone
    TurnSolver::Plan p;
    p.land_decided = true;
    p.land_to_play = "Teetering Peaks";
    TurnSolver::ApplyPlan(s, p, true);
    const Permanent* c = Cb3Find(s, "Cinder Pyromancer");
    REQUIRE(c != nullptr);
    CHECK(c->temp_power_bonus == 2);     // the target is mandatory
    CHECK(TurnSolver::ProjectedAttackDamageForTest(s) == 0);   // it cannot attack (or tap) this turn
    CHECK(s.players[1].life == 20);
}

TEST_CASE("Teetering Peaks: no creature -> it enters tapped and the trigger is removed (CR 603.3d)")
{
    GameState s = Cb3Board(2, { "Teetering Peaks" });
    TurnSolver::Plan p;
    p.land_decided = true;
    p.land_to_play = "Teetering Peaks";
    TurnSolver::ApplyPlan(s, p, true);
    const Permanent* pk = Cb3Find(s, "Teetering Peaks");
    REQUIRE(pk != nullptr);
    CHECK(pk->tapped);
    CHECK(s.players[0].lands_played_this_turn == 1);
    for (const Permanent& q : s.battlefield) { CHECK(q.temp_power_bonus == 0); }
    // The effect-put route resolves the same trigger (EnterLand).
    GameState t = Cb3Board(0, {});
    Cb3Put(t, "Spark Elemental", 22);
    EnterLand(t, Cb3Def("Teetering Peaks"), 90);
    const Permanent* sp = Cb3Find(t, "Spark Elemental");
    REQUIRE(sp != nullptr);
    CHECK(sp->temp_power_bonus == 2);
}

TEST_CASE("Teetering Peaks: the fold classes (identity, inert, tapped-is-not-inert, after combat)")
{
    GameState s = Cb3Board(0, {});
    Cb3Put(s, "Goblin Fireslinger", 30);
    Cb3Put(s, "Goblin Fireslinger", 31);                         // identical -> one class
    Cb3Put(s, "Spark Elemental", 32);
    Cb3Put(s, "Keldon Marauders", 33, /*sick=*/true);            // summoning-sick, no haste -> inert
    Cb3Put(s, "Ball Lightning", 34, /*sick=*/false, /*controller=*/1);   // the opponent's -> inert
    Permanent& py = Cb3Put(s, "Cinder Pyromancer", 35);
    py.tapped = true;                                            // tapped is NOT inert (it untaps on a red cast)
    std::vector<std::string> names = Cb3RepNames(s);             // base provider: battlefield order, nothing pruned
    CHECK(names == std::vector<std::string>{ "Goblin Fireslinger", "Spark Elemental", "Cinder Pyromancer" });

    // CreatureBurnProvider: plain attackers first, pingers after; one plain class stands for all of them.
    const CreatureBurnProvider prov;
    s.m_provider = &prov;
    Cb3Put(s, "Keldon Champion", 36);                            // a second, distinct plain attacker
    names = Cb3RepNames(s);
    CHECK(names == std::vector<std::string>{ "Spark Elemental", "Goblin Fireslinger", "Cinder Pyromancer" });

    // The searched rank picks among them (the shared whole-turn pin).
    {
        GameState t = s;
        ScriptedLandfallTarget pin(2, t.TurnSeq());
        ResolveLandEtbTargetPump(t, 0, Cb3Def("Teetering Peaks"));
        const Permanent* c = Cb3Find(t, "Cinder Pyromancer");
        REQUIRE(c != nullptr);
        CHECK(c->temp_power_bonus == 2);
    }
    {
        GameState t = s;
        ScriptedLandfallTarget pin(9, t.TurnSeq());              // out of range clamps to the last class
        ResolveLandEtbTargetPump(t, 0, Cb3Def("Teetering Peaks"));
        CHECK(Cb3Find(t, "Cinder Pyromancer")->temp_power_bonus == 2);
    }
    {
        GameState t = s;                                         // no pin: the provider's front
        ResolveLandEtbTargetPump(t, 0, Cb3Def("Teetering Peaks"));
        CHECK(Cb3Find(t, "Spark Elemental")->temp_power_bonus == 2);
    }

    // Once combat is over every candidate is inert: one class.
    GameState m2 = s;
    m2.phase = Phase::PostCombatMain;
    CHECK(Cb3RepNames(m2).size() == 1);
}

TEST_CASE("Teetering Peaks: the target axis fans landfall_target_choice over the classes on a stamped deck only")
{
    GameState s = Cb3Board(2, { "Teetering Peaks" });
    Cb3Put(s, "Goblin Fireslinger", 30);
    Cb3Put(s, "Spark Elemental", 32);
    TurnSolver::Plan p;
    p.land_decided = true;
    p.land_to_play = "Teetering Peaks";
    std::vector<TurnSolver::Plan> out = TurnSolver::SubdecisionAxesForTest(s, true, { p });
    bool rank1 = false;
    for (const TurnSolver::Plan& q : out) { if (q.landfall_target_choice == 1) { rank1 = true; } }
    CHECK(rank1);
    GameState u = s;
    u.deck_has_land_etb_target = false;
    for (const TurnSolver::Plan& q : TurnSolver::SubdecisionAxesForTest(u, true, { p }))
    { CHECK(q.landfall_target_choice < 0); }
    GameState m2 = s;
    m2.phase = Phase::PostCombatMain;
    for (const TurnSolver::Plan& q : TurnSolver::SubdecisionAxesForTest(m2, false, { p }))
    { CHECK(q.landfall_target_choice < 0); }
}
