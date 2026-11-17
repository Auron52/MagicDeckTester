// Sea Creatures main-phase shape (MTG_SEA_MAIN1_KRAKEN_ONLY; USER 2026-11-17: "So we only do Main 1 if Spawning
// Kraken is cast there, in which case the mana spells can also be done."). SeaCreaturesProvider::Main1PlanAdmitted
// keeps a root main-1 plan only when it casts Spawning Kraken, and then only alongside mana spells that pay back this
// turn (land Auras, an untapped rock making at least its cost); MTG_SEA_MAIN1_KRAKEN_ATTACKER further requires a
// watched-subtype creature that can attack this turn. GoldFishRunner::DeckUsesSecondMain turns the deck's main 2 on.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "deck/DeckLoader.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "runner/GoldFishRunner.h"

#include <string>
#include <vector>

namespace
{
const CardDefinition& DefSm(const std::string& n)
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const CardDefinition* d = CardDatabase::Instance().Lookup(n);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", n);
    return *d;
}
Card CardSm(const std::string& name, int number)
{
    Card c     = DefSm(name).card;
    c.m_number = number;
    return c;
}
Permanent PermSm(const std::string& name, int number, bool fresh = false)
{
    Permanent p;
    p.card              = CardSm(name, number);
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = fresh;
    return p;
}
const SeaCreaturesProvider& ProvSm() { static const SeaCreaturesProvider p; return p; }

struct ArmSm
{
    heurarm::Arm saved;
    ArmSm() : saved(heurarm::t_arm) {}
    ArmSm& Set(heurarm::Slot slot, int v) { heurarm::t_arm[static_cast<int>(slot)] = static_cast<std::int8_t>(v); return *this; }
    ~ArmSm() { heurarm::t_arm = saved; }
};
struct SearchedSm
{
    bool saved;
    SearchedSm() : saved(g_searched_play) { TurnSolver::SetSearchedPlay(true); }
    ~SearchedSm() { TurnSolver::SetSearchedPlay(saved); }
};
Action CastSm(const std::string& name)
{
    Action a;
    a.kind      = Action::Kind::CastFromHand;
    a.def       = &DefSm(name);
    a.card_name = DefSm(name).card.m_name;
    return a;
}
GameState BaseSm()
{
    GameState s;
    s.m_provider          = &ProvSm();
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PreCombatMain;
    s.uses_second_main    = true;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    for (int k = 0; k < 4; ++k) { s.battlefield.push_back(PermSm("Forest", 1 + k)); }
    for (int k = 0; k < 2; ++k) { s.battlefield.push_back(PermSm("Island", 10 + k)); }
    return s;
}
}   // namespace

TEST_CASE("Sea main 1 (MTG_SEA_MAIN1_KRAKEN_ONLY): only a Spawning Kraken plan casts in main 1, with payback mana")
{
    const GameState s = BaseSm();
    const SeaCreaturesProvider& p = ProvSm();
    const std::string no_land;
    SUBCASE("OFF: nothing is folded")
    {
        ArmSm arm; arm.Set(heurarm::SEA_MAIN1_KRAKEN_ONLY, 0);
        CHECK_FALSE(p.Main1PlanFoldLive(s));
    }
    ArmSm arm; arm.Set(heurarm::SEA_MAIN1_KRAKEN_ONLY, 1).Set(heurarm::SEA_MAIN1_KRAKEN_ATTACKER, 0);
    REQUIRE(p.Main1PlanFoldLive(s));
    CHECK(p.Main1PlanAdmitted(s, {}, ""));                                                        // pass to combat
    CHECK_FALSE(p.Main1PlanAdmitted(s, {}, "Forest"));                                            // its drop is main 2's
    CHECK(p.Main1LandDropFolded(s));
    CHECK(p.Main1PlanAdmitted(s, { CastSm("Spawning Kraken") }, no_land));
    CHECK(p.Main1PlanAdmitted(s, { CastSm("Wild Growth"), CastSm("Spawning Kraken") }, no_land));
    CHECK(p.Main1PlanAdmitted(s, { CastSm("Utopia Sprawl"), CastSm("Overgrowth"), CastSm("Spawning Kraken") }, no_land));
    CHECK(p.Main1PlanAdmitted(s, { CastSm("Sol Ring"), CastSm("Spawning Kraken") }, no_land));
    CHECK_FALSE(p.Main1PlanAdmitted(s, { CastSm("Worn Powerstone"), CastSm("Spawning Kraken") }, no_land));  // tapped
    CHECK_FALSE(p.Main1PlanAdmitted(s, { CastSm("Spawning Kraken"), CastSm("Serpent of Yawning Depths") }, no_land));
    CHECK_FALSE(p.Main1PlanAdmitted(s, { CastSm("Wild Growth") }, no_land));                      // no Kraken: main 2
    CHECK_FALSE(p.Main1PlanAdmitted(s, { CastSm("Sol Ring") }, no_land));
    CHECK_FALSE(p.Main1PlanAdmitted(s, { CastSm("Koma, World-Eater") }, no_land));
    // A land in hand with the drop open: the Kraken plan must carry it (one place for the drop).
    GameState h = BaseSm();
    h.players[0].hand.push_back(CardSm("Island", 60));
    CHECK_FALSE(p.Main1PlanAdmitted(h, { CastSm("Spawning Kraken") }, no_land));
    CHECK(p.Main1PlanAdmitted(h, { CastSm("Spawning Kraken") }, "Island"));
}

TEST_CASE("Sea main 1 (MTG_SEA_MAIN1_KRAKEN_ATTACKER): the Kraken plan needs a watched-subtype creature that can attack")
{
    const SeaCreaturesProvider& p = ProvSm();
    ArmSm arm; arm.Set(heurarm::SEA_MAIN1_KRAKEN_ONLY, 1).Set(heurarm::SEA_MAIN1_KRAKEN_ATTACKER, 1);
    const std::vector<Action> plan = { CastSm("Wild Growth"), CastSm("Spawning Kraken") };
    GameState none = BaseSm();
    CHECK_FALSE(p.Main1PlanAdmitted(none, plan, ""));
    GameState sick = BaseSm();
    sick.battlefield.push_back(PermSm("Serpent of Yawning Depths", 40, /*fresh=*/true));
    CHECK_FALSE(p.Main1PlanAdmitted(sick, plan, ""));
    GameState ready = BaseSm();
    ready.battlefield.push_back(PermSm("Serpent of Yawning Depths", 40));
    CHECK(p.Main1PlanAdmitted(ready, plan, ""));
    GameState koma = BaseSm();                                        // Koma is a Serpent
    koma.battlefield.push_back(PermSm("Koma, World-Eater", 41));
    CHECK(p.Main1PlanAdmitted(koma, plan, ""));
}

TEST_CASE("Sea main 1: the root enumeration keeps main 1 to the land drop unless the Kraken is cast")
{
    SearchedSm sp;
    ArmSm arm; arm.Set(heurarm::SEA_MAIN1_KRAKEN_ONLY, 1).Set(heurarm::SEA_MAIN1_KRAKEN_ATTACKER, 0);
    GameState s = BaseSm();
    s.players[0].hand.push_back(CardSm("Forest", 60));
    s.players[0].hand.push_back(CardSm("Wild Growth", 61));
    s.players[0].hand.push_back(CardSm("Serpent of Yawning Depths", 62));
    int with_actions = 0, with_land = 0, plans = 0;
    for (const TurnSolver::Plan& pl : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        ++plans;
        if (!pl.actions.empty()) { ++with_actions; }
        if (!pl.land_to_play.empty()) { ++with_land; }
    }
    CHECK(plans > 0);
    CHECK(with_actions == 0);
    CHECK(with_land == 0);      // the Forest is main 2's drop
    s.players[0].hand.push_back(CardSm("Spawning Kraken", 63));
    int kraken = 0, other = 0;
    for (const TurnSolver::Plan& pl : TurnSolver::EnumerateMainPlans(s, true))
    {
        bool k = false, bad = false;
        for (const Action& a : pl.actions)
        {
            if (a.kind != Action::Kind::CastFromHand) { continue; }
            if (a.card_name.str() == "Spawning Kraken") { k = true; }
            else if (a.card_name.str() != "Wild Growth") { bad = true; }
        }
        if (k) { ++kraken; CHECK(pl.land_to_play == "Forest"); }
        if (!pl.actions.empty() && (!k || bad)) { ++other; }
        if (!k) { CHECK(pl.land_to_play.empty()); }
    }
    CHECK(kraken > 0);
    CHECK(other == 0);
}

TEST_CASE("Sea main 1: a land held since main 1 is offered in main 2 only under the rule")
{
    SearchedSm sp;
    GameState s = BaseSm();
    s.phase = Phase::PostCombatMain;
    const Card forest = CardSm("Forest", 60);
    s.players[0].hand.push_back(forest);
    s.m1start_hand_turn = s.TurnSeq();
    s.m1start_hand_mask[forest.m_number >> 6] |= 1ULL << (forest.m_number & 63);
    REQUIRE(s.HeldSinceMain1(forest));
    auto lands = [&]
    {
        int n = 0;
        for (const TurnSolver::Plan& pl : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/false))
        { if (pl.land_to_play == "Forest") { ++n; } }
        return n;
    };
    {
        ArmSm arm; arm.Set(heurarm::SEA_MAIN1_KRAKEN_ONLY, 0);
        CHECK(lands() == 0);    // the engine's held-land bar
    }
    ArmSm arm; arm.Set(heurarm::SEA_MAIN1_KRAKEN_ONLY, 1);
    CHECK(lands() > 0);
}

TEST_CASE("Sea main 1: the flag gives Sea Creatures a main 2 every turn, and no other deck")
{
    (void)DefSm("Forest");
    const Decklist sea = DeckLoader::LoadFromFile(ResolveHeuristicDefaultsPath("decks/Sea Creatures/Sea Creatures.cod"));
    const Decklist mer = DeckLoader::LoadFromFile(ResolveHeuristicDefaultsPath("decks/Merfolk/Merfolk.cod"));
    {
        ArmSm arm; arm.Set(heurarm::SEA_MAIN1_KRAKEN_ONLY, 0);
        CHECK_FALSE(GoldFishRunner::DeckUsesSecondMain(sea));
    }
    ArmSm arm; arm.Set(heurarm::SEA_MAIN1_KRAKEN_ONLY, 1);
    CHECK(GoldFishRunner::DeckUsesSecondMain(sea));
    const bool mer_off = [&] { ArmSm a2; a2.Set(heurarm::SEA_MAIN1_KRAKEN_ONLY, 0); return GoldFishRunner::DeckUsesSecondMain(mer); }();
    CHECK(GoldFishRunner::DeckUsesSecondMain(mer) == mer_off);
}

TEST_CASE("Sea main 2 on a Kraken turn: held Kraken / payback mana are main 1's; a card drawn after main 1 is offered")
{
    SearchedSm sp;
    auto build = [](bool kraken_turn)
    {
        GameState s = BaseSm();
        s.phase = Phase::PostCombatMain;
        s.m1start_hand_turn = s.TurnSeq();
        auto held = [&s](const Card& c) { s.m1start_hand_mask[c.m_number >> 6] |= 1ULL << (c.m_number & 63); };
        if (kraken_turn)
        {
            Permanent k = PermSm("Spawning Kraken", 30, /*fresh=*/true);
            held(k.card);
            s.battlefield.push_back(k);
        }
        s.battlefield_at_combat = static_cast<int>(s.battlefield.size());
        const Card old_wg = CardSm("Wild Growth", 61);
        held(old_wg);
        s.players[0].hand.push_back(old_wg);                    // held since main 1
        s.players[0].hand.push_back(CardSm("Wild Growth", 62)); // drawn in combat (Leviathan)
        const Card serp = CardSm("Serpent of Yawning Depths", 63);
        held(serp);
        s.players[0].hand.push_back(serp);                      // never main-1 eligible
        return s;
    };
    auto offered = [](const GameState& s, int number)
    {
        for (const Action& a : TurnSolver::CollectActionsForTest(s, /*pre_combat=*/false))
        {
            if (a.kind != Action::Kind::CastFromHand || a.hand_index < 0) { continue; }
            if (s.players[0].hand[static_cast<std::size_t>(a.hand_index)].m_number == number) { return true; }
        }
        return false;
    };
    ArmSm arm; arm.Set(heurarm::SEA_MAIN1_KRAKEN_ONLY, 1);
    const GameState kt = build(true);
    CHECK((offered(kt, 61) || offered(kt, 62)));     // the drawn copy stays castable (copies may share one option)
    GameState only_held = kt;                          // ...but with no drawn copy, the held one is main 1's
    only_held.players[0].hand.erase(only_held.players[0].hand.begin() + 1);
    CHECK_FALSE(offered(only_held, 61));
    CHECK(offered(kt, 63));
    const GameState other = build(false);
    CHECK((offered(other, 61) || offered(other, 62)));
    CHECK(offered(other, 63));
}
