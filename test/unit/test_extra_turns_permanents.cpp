#include <doctest/doctest.h>
#include "cards/CardDatabase.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "ai/LandPlay.h"
#include "ai/ManaPayment.h"
#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"

namespace {
void Load()
{
    static const bool ready = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(ready);
}
const CardDefinition& Def(const char* name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    return *d;
}
Card Copy(const char* name, int id)
{
    Card c = Def(name).card;
    c.m_number = id;
    return c;
}
void Put(GameState& s, const char* name, int id, bool tapped = false)
{
    Permanent p;
    p.card = Copy(name, id);
    p.owner_index = p.controller_index = 0;
    p.tapped = tapped;
    s.battlefield.push_back(p);
}
}

TEST_CASE("Extra Turns Lens imprints a physical Snow Island and adds ordinary blue")
{
    Load();
    GameState s;
    Put(s, "Extraplanar Lens", 10);
    Put(s, "Snow-Covered Island", 11);
    Put(s, "Snow-Covered Island", 12);
    FireOwnEtbTriggers(s, 0, 0, "", 11);
    REQUIRE(s.battlefield.size() == 2);
    CHECK(s.exile.size() == 1);
    CHECK(s.exile.front().m_number == 11);
    CHECK(s.battlefield.front().lens_imprinted_land.str() == "Snow-Covered Island");
    ManaPool projected;
    AddSourceToPool(projected, s, Def("Snow-Covered Island"), -1, &s.battlefield.back());
    CHECK(projected.blue == 2);
    CHECK(projected.snow_units == 1);
    GameState paid = s;
    REQUIRE(Def("Temporal Mastery").params.miracle_cost.has_value());
    CHECK(TapForCostDirect(paid, *Def("Temporal Mastery").params.miracle_cost, false));
    CHECK(paid.battlefield.back().tapped);
    ManaPool actual;
    TapSourceIntoFloat(s, 0, s.battlefield.back(), Def("Snow-Covered Island"),
                       Color::Blue, actual, nullptr, false);
    CHECK(actual.blue == 2);
    CHECK(s.battlefield.back().tapped);
}

TEST_CASE("Extra Turns searched Lens imprint gives the rollout its future mana")
{
    Load();
    GameState s;
    s.turn_number = 4;
    for (int i = 0; i < 4; ++i) { Put(s, "Snow-Covered Island", 11 + i); }
    s.players[0].hand.push_back(Copy("Extraplanar Lens", 20));
    TurnSolver::Plan p;
    p.land_decided = true;
    Action a;
    a.kind = Action::Kind::CastFromHand;
    a.card_name = "Extraplanar Lens";
    a.chosen_x = 11;
    p.actions.push_back(a);
    TurnSolver::ApplyPlan(s, p, true);
    REQUIRE(s.exile.size() == 1);
    CHECK(s.exile.front().m_number == 11);
    int snow = 0;
    for (Permanent& perm : s.battlefield)
    {
        if (perm.card.m_name == "Snow-Covered Island") { ++snow; perm.tapped = false; }
    }
    CHECK(snow == 3);
    CHECK(AvailableManaPool(s).Total() == 6);
}

TEST_CASE("Extra Turns orders a mana-beneficial Lens before Stone with Horn and a turn spell")
{
    Load();
    ExtraTurnsProvider policy;
    GameState s;
    s.m_provider = &policy;
    s.turn_number = 5;
    Put(s, "Snow-Covered Island", 100, true); // imprint a land already tapped
    for (int i = 0; i < 8; ++i) { Put(s, "Snow-Covered Island", 101 + i); }
    auto cast = [&](const char* name, int id)
    {
        s.players[0].hand.push_back(Copy(name, id));
        Action a;
        a.kind = Action::Kind::CastFromHand;
        a.card_name = name;
        a.def = &Def(name);
        a.cost = a.def->card.m_mana_cost;
        if (a.def->params.lens_imprint_land) { a.chosen_x = 100; }
        return a;
    };
    const std::vector<Action> acts{
        cast("Mind Stone", 20), cast("Extraplanar Lens", 21),
        cast("Horn of Greed", 22), cast("Time Warp", 23)};
    std::vector<int> order{0, 1, 2, 3};
    CHECK(TurnSolver::OrderPaysSequentially(s, acts, order));
    policy.ArrangeCastOrder(s, acts, order);
    CHECK(order == std::vector<int>{1, 0, 2, 3});
    CHECK(TurnSolver::OrderPaysSequentially(s, acts, order));
    std::vector<Action> decline = acts;
    decline[1].chosen_x = 0;
    CHECK_FALSE(TurnSolver::OrderPaysSequentially(s, decline, order));
    std::vector<int> decline_order{0, 1, 2, 3};
    policy.ArrangeCastOrder(s, decline, decline_order);
    CHECK(decline_order == std::vector<int>{0, 1, 2, 3});
}

TEST_CASE("Extra Turns chain trial takes Emrakul and protects Ulamog's attack turn")
{
    Load();
    ExtraTurnsProvider policy;
    GameState s;
    for (int i = 0; i < 15; ++i) { Put(s, "Snow-Covered Island", 100 + i); }
    s.players[0].hand.push_back(Copy("Emrakul, the Aeons Torn", 1));
    CHECK_FALSE(policy.ExtraTurnChainFinisherPlanAllowed(s, false, false, true));
    CHECK(policy.ExtraTurnChainFinisherPlanAllowed(s, true, false, false));

    s.players[0].hand.clear();
    CHECK_FALSE(policy.ExtraTurnChainFinisherPlanAllowed(s, false, true, false));
    CHECK(policy.ExtraTurnChainFinisherPlanAllowed(s, false, true, true));
    s.pending_extra_turns = 1;
    CHECK(policy.ExtraTurnChainFinisherPlanAllowed(s, false, true, false));
}

TEST_CASE("Extra Turns cleanup buckets pitch surplus engines before chain spells and finishers")
{
    Load();
    ExtraTurnsProvider policy;
    GameState s;
    for (int i = 0; i < 8; ++i) { Put(s, "Snow-Covered Island", 100 + i); }
    Put(s, "Howling Mine", 200);
    Put(s, "Horn of Greed", 201);
    Put(s, "Horn of Greed", 203);
    Put(s, "Extraplanar Lens", 202);
    s.battlefield.back().lens_imprinted_land = Def("Snow-Covered Island").card.m_name;
    const char* names[] = {"Emrakul, the Aeons Torn", "Ulamog, the Ceaseless Hunger",
                           "Time Warp", "Walk the Aeons", "Horn of Greed", "Howling Mine",
                           "Extraplanar Lens", "Myriad Landscape", "Snow-Covered Island"};
    for (int i = 0; i < 9; ++i) { s.players[0].hand.push_back(Copy(names[i], 300 + i)); }
    const std::vector<int> order = policy.CleanupDiscardCandidates(s, nullptr);
    REQUIRE(order.size() == 9);
    CHECK(s.players[0].hand[static_cast<std::size_t>(order[0])].m_name == "Horn of Greed");
    CHECK(s.players[0].hand[static_cast<std::size_t>(order[1])].m_name == "Howling Mine");
    CHECK(s.players[0].hand[static_cast<std::size_t>(order[2])].m_name == "Extraplanar Lens");
    for (int i = 0; i < 5; ++i)
    {
        const auto& name = s.players[0].hand[static_cast<std::size_t>(order[i])].m_name;
        CHECK(name != "Emrakul, the Aeons Torn");
        CHECK(name != "Ulamog, the Ceaseless Hunger");
        CHECK(name != "Time Warp");
        CHECK(name != "Walk the Aeons");
    }

    GameState early;
    early.players[0].hand.push_back(Copy("Horn of Greed", 401));
    early.players[0].hand.push_back(Copy("Howling Mine", 402));
    early.players[0].hand.push_back(Copy("Horn of Greed", 403));
    early.players[0].hand.push_back(Copy("Howling Mine", 404));
    early.players[0].hand.push_back(Copy("Temporal Manipulation", 405));
    const std::vector<int> early_order = policy.CleanupDiscardCandidates(early, nullptr);
    REQUIRE(early_order.size() == 5);
    CHECK(early_order[0] == 2); // fourth engine is spare; both Mines are kept
    CHECK(early_order.back() == 4); // an extra turn is never a spare engine

    GameState horn_only;
    for (int i = 0; i < 3; ++i) { Put(horn_only, "Horn of Greed", 450 + i); }
    horn_only.players[0].hand.push_back(Copy("Howling Mine", 453));
    horn_only.players[0].hand.push_back(Copy("Horn of Greed", 454));
    const std::vector<int> horn_only_order = policy.CleanupDiscardCandidates(horn_only, nullptr);
    REQUIRE(horn_only_order.size() == 2);
    CHECK(horn_only_order[0] == 1); // keep Mine as the upgrade to three Horns

    GameState flooded_chain;
    flooded_chain.players[0].hand.push_back(Copy("Emrakul, the Aeons Torn", 470));
    flooded_chain.players[0].hand.push_back(Copy("Ulamog, the Ceaseless Hunger", 471));
    for (int i = 0; i < 6; ++i)
    { flooded_chain.players[0].hand.push_back(Copy("Time Warp", 472 + i)); }
    flooded_chain.players[0].hand.push_back(Copy("Walk the Aeons", 478));
    flooded_chain.players[0].hand.push_back(Copy("Expropriate", 479));
    const std::vector<int> flooded_order = policy.CleanupDiscardCandidates(flooded_chain, nullptr);
    REQUIRE(flooded_order.size() == 10);
    for (int i = 0; i < 5; ++i)
    { CHECK(flooded_order[static_cast<std::size_t>(i)] >= 2); }
    CHECK(flooded_order[5] == 1); // Ulamog and Emrakul outlast five surplus turn spells
    CHECK(flooded_order[6] == 0);
    CHECK(flooded_order.back() == 8); // retain one buyback Walk for the end of the chain

    GameState lens_to_finisher;
    for (int i = 0; i < 4; ++i) { Put(lens_to_finisher, "Snow-Covered Island", 480 + i); }
    lens_to_finisher.players[0].hand.push_back(Copy("Emrakul, the Aeons Torn", 484));
    lens_to_finisher.players[0].hand.push_back(Copy("Extraplanar Lens", 485));
    lens_to_finisher.players[0].hand.push_back(Copy("Howling Mine", 486));
    for (int i = 0; i < 4; ++i)
    { lens_to_finisher.players[0].hand.push_back(Copy("Time Warp", 487 + i)); }
    const std::vector<int> lens_order = policy.CleanupDiscardCandidates(lens_to_finisher, nullptr);
    REQUIRE(lens_order.size() == 7);
    CHECK(lens_order[0] == 6); // one spare Warp before the Lens needed for Emrakul
    CHECK(lens_order[1] == 1);
    CHECK(lens_order[2] == 2); // the first draw engine stays ahead of Lens

    GameState two_lenses;
    two_lenses.players[0].hand.push_back(Copy("Extraplanar Lens", 491));
    two_lenses.players[0].hand.push_back(Copy("Extraplanar Lens", 492));
    two_lenses.players[0].hand.push_back(Copy("Time Warp", 493));
    const std::vector<int> lens_copies = policy.CleanupDiscardCandidates(two_lenses, nullptr);
    REQUIRE(lens_copies.size() == 3);
    CHECK(lens_copies[0] == 1); // keep only one Lens when neither is on the board
    Put(two_lenses, "Extraplanar Lens", 494);
    const std::vector<int> lens_with_board = policy.CleanupDiscardCandidates(two_lenses, nullptr);
    CHECK(lens_with_board[0] == 0);
    CHECK(lens_with_board[1] == 1); // a board Lens makes both hand copies spare

    GameState mana;
    for (int i = 0; i < 3; ++i) { Put(mana, "Snow-Covered Island", 500 + i); }
    for (int i = 0; i < 5; ++i)
    { mana.players[0].hand.push_back(Copy("Snow-Covered Island", 600 + i)); }
    mana.players[0].hand.push_back(Copy("Time Warp", 605));
    const std::vector<int> mana_early = policy.CleanupDiscardCandidates(mana, nullptr);
    REQUIRE(mana_early.size() == 6);
    CHECK(mana_early[0] == 4); // four land drops remain protected toward seven mana
    mana.in_extra_turn = true;
    const std::vector<int> mana_early_chain = policy.CleanupDiscardCandidates(mana, nullptr);
    REQUIRE(mana_early_chain.size() == 6);
    CHECK(mana_early_chain[0] == 4); // an early miracle turn still needs its mana sources
    mana.turn_number = 5;
    const std::vector<int> mana_late = policy.CleanupDiscardCandidates(mana, nullptr);
    REQUIRE(mana_late.size() == 6);
    CHECK(mana_late[0] == 4); // four drops remain needed to reach seven
    CHECK(mana_late.back() == 5);
    GameState six_mana;
    for (int i = 0; i < 6; ++i) { Put(six_mana, "Snow-Covered Island", 700 + i); }
    for (int i = 0; i < 3; ++i)
    { six_mana.players[0].hand.push_back(Copy("Snow-Covered Island", 710 + i)); }
    six_mana.turn_number = 6;
    const std::vector<int> six_mana_order = policy.CleanupDiscardCandidates(six_mana, nullptr);
    REQUIRE(six_mana_order.size() == 3);
    CHECK(six_mana_order[0] == 2); // keep two even when only one more reaches seven
    Put(six_mana, "Snow-Covered Island", 706);
    const std::vector<int> seven_mana_order = policy.CleanupDiscardCandidates(six_mana, nullptr);
    REQUIRE(seven_mana_order.size() == 3);
    CHECK(seven_mana_order[0] == 2); // without Mine, reserve two drops
    Put(six_mana, "Howling Mine", 707);
    const std::vector<int> seven_mana_mine_order = policy.CleanupDiscardCandidates(six_mana, nullptr);
    REQUIRE(seven_mana_mine_order.size() == 3);
    CHECK(seven_mana_mine_order[0] == 1);
    CHECK(seven_mana_mine_order[1] == 2); // Mine draws enough to reserve one
}

TEST_CASE("Ulamog attack exiles defender library before combat damage")
{
    Load();
    GameState s;
    s.opponent_library_dealt = true;
    s.players[1].life = 21; // Ulamog's combat damage cannot win this attack.
    for (int n = 0; n < 20; ++n) { s.players[1].library.push_back(Copy("Snow-Covered Island", 100 + n)); }
    Put(s, "Ulamog, the Ceaseless Hunger", 1);
    CHECK(Def("Ulamog, the Ceaseless Hunger").params.attack_exile_defender_top == 20);
    ApplyAttackExileDefenderTop(s, 0, {}); // Present, but not declared as an attacker.
    CHECK(s.players[1].library.size() == 20);
    CHECK_FALSE(s.opponent_decked);
    ApplyAttackExileDefenderTop(s, 0, {0});
    CHECK(s.players[1].library.empty());
    CHECK(s.exile.size() == 20);
    CHECK(s.opponent_decked);
}

TEST_CASE("Extra Turns Lens may decline imprint and then adds nothing")
{
    Load();
    GameState s;
    Put(s, "Extraplanar Lens", 10);
    Put(s, "Snow-Covered Island", 11);
    FireOwnEtbTriggers(s, 0, 0, "", 0);
    CHECK(s.exile.empty());
    ManaPool projected;
    AddSourceToPool(projected, s, Def("Snow-Covered Island"), -1, &s.battlefield.back());
    CHECK(projected.blue == 1);
}

TEST_CASE("A gained opponent basic land supplies conservative colorless mana")
{
    Load();
    GameState s;
    Permanent gained;
    gained.card = Def("Opponent Basic Land").card;
    gained.owner_index = 1;
    gained.controller_index = 0;
    s.battlefield.push_back(gained);
    ManaPool projected;
    AddSourceToPool(projected, s, Def("Opponent Basic Land"), -1, &s.battlefield.front());
    CHECK(projected.colorless == 1);
    for (int i = 0; i < 4; ++i) { Put(s, "Snow-Covered Island", 100 + i); }
    CHECK(TapForCostDirect(s, Def("Time Warp").card.m_mana_cost, false));
    CHECK(s.battlefield.front().tapped);
}

TEST_CASE("Extra Turns second Lens setup credit requires current-turn mana payback")
{
    Load();
    ExtraTurnsProvider policy;
    GameState s;
    Put(s, "Extraplanar Lens", 900);
    s.battlefield.back().lens_imprinted_land = Def("Snow-Covered Island").card.m_name;
    for (int i = 0; i < 4; ++i) { Put(s, "Snow-Covered Island", 901 + i); }
    CHECK(policy.LensImprintValueBonus(s, s.battlefield[1]) < 0);
    Put(s, "Snow-Covered Island", 905);
    CHECK(policy.LensImprintValueBonus(s, s.battlefield[1]) >= 0);
}

TEST_CASE("Extra Turns Horn draws on a played land and not a put land")
{
    Load();
    GameState s;
    Put(s, "Horn of Greed", 10);
    s.players[0].hand.push_back(Copy("Snow-Covered Island", 11));
    s.players[0].library.push_back(Copy("Time Warp", 20));
    s.players[0].library.push_back(Copy("Temporal Mastery", 21));
    LandPlayOptions o;
    REQUIRE(PlayLandFromHand(s, 0, Def("Snow-Covered Island"), o));
    CHECK(s.players[0].hand.size() == 1);
    CHECK(s.players[0].hand.front().m_number == 20);
    EnterLand(s, Def("Snow-Covered Island"), 12, true, true, LandEntryTap::Natural);
    CHECK(s.players[0].hand.size() == 1);
    CHECK(s.players[0].library.size() == 1);
}

TEST_CASE("A Mine draw that decks us stops the rollout before the next main")
{
    Load();
    GameState s;
    s.turn_number = 2;
    Put(s, "Howling Mine", 10);
    s.players[0].library.push_back(Copy("Time Warp", 20));
    CHECK_FALSE(TurnSolver::SimulateEndAndStartNextTurnForTest(s));
    CHECK(s.player_lost_on_draw);
    CHECK(s.players[0].hand.size() == 1);
}

TEST_CASE("Extra Turns Horn and Mine draws appear in play history")
{
    Load();
    GameState s;
    Put(s, "Horn of Greed", 10);
    Put(s, "Howling Mine", 11);
    s.players[0].hand.push_back(Copy("Snow-Covered Island", 12));
    s.players[0].library.push_back(Copy("Time Warp", 20));
    s.players[0].library.push_back(Copy("Temporal Mastery", 21));
    std::vector<std::pair<int, std::string>> draws;
    auto* old_sink = g_play_draw_sink;
    g_play_draw_sink = &draws;
    LandPlayOptions o;
    const bool played = PlayLandFromHand(s, 0, Def("Snow-Covered Island"), o);
    DrawHowlingMineCards(s, 0);
    g_play_draw_sink = old_sink;
    REQUIRE(played);
    REQUIRE(draws.size() == 2);
    CHECK(draws[0].second == "Time Warp");
    CHECK(draws[1].second == "Temporal Mastery");
}

TEST_CASE("Extra Turns can cast Horn before its land drop")
{
    Load();
    GameState s;
    s.turn_number = 3;
    s.deck_has_landfall = true;
    for (int i = 0; i < 3; ++i) { Put(s, "Snow-Covered Island", 10 + i); }
    s.players[0].hand.push_back(Copy("Horn of Greed", 20));
    s.players[0].hand.push_back(Copy("Snow-Covered Island", 21));
    s.players[0].library.push_back(Copy("Time Warp", 22));
    bool found = false;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        if (p.land_slot != TurnSolver::kLandSlotAfterCasts || p.land_to_play != "Snow-Covered Island")
        { continue; }
        if (p.actions.size() != 1 || p.actions.front().card_name != "Horn of Greed") { continue; }
        GameState played = s;
        TurnSolver::ApplyPlan(played, p, true);
        CHECK(played.players[0].hand.size() == 1);
        CHECK(played.players[0].hand.front().m_number == 22);
        found = true;
        break;
    }
    CHECK(found);
}

TEST_CASE("Extra Turns Mine respects tapping and opponent's additional draw")
{
    Load();
    GameState s;
    Put(s, "Howling Mine", 10, true);
    CHECK(UntappedHowlingMineCount(s) == 0);
    s.battlefield[0].tapped = false;
    Put(s, "Howling Mine", 11);
    CHECK(UntappedHowlingMineCount(s) == 2);
    s.opponent_library_dealt = true;
    s.players[1].library.push_back(Copy("Snow-Covered Island", 21));
    s.players[1].library.push_back(Copy("Time Warp", 22));
    DrawHowlingMineCards(s, 1);
    CHECK(s.players[1].hand.size() == 2);
    CHECK_FALSE(s.opponent_decked);
    DrawHowlingMineCards(s, 1);
    CHECK(s.opponent_decked);
}

TEST_CASE("Extra Turns Myriad searches zero one or two and can choose zero with shuffle")
{
    Load();
    GameState s;
    s.players[0].library.push_back(Copy("Snow-Covered Island", 21));
    s.players[0].library.push_back(Copy("Snow-Covered Island", 22));
    const LandSearchSpec spec = LandSearchSpecOf(Def("Myriad Landscape").params);
    const auto picks = LandSearchPicks(s, 0, spec);
    REQUIRE(picks.size() == 3);
    int zero = -1;
    bool one = false, two = false;
    for (int i = 0; i < static_cast<int>(picks.size()); ++i)
    {
        if (picks[i].empty()) { zero = i; }
        one |= picks[i].size() == 1;
        two |= picks[i].size() == 2;
    }
    CHECK(one); CHECK(two);
    REQUIRE(zero >= 0);
    PerformLandSearch(s, 0, spec, "Myriad Landscape", zero);
    CHECK(s.players[0].library.size() == 2);
    CHECK(s.battlefield.empty());
}
