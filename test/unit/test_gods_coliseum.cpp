#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "ai/TurnSolver.h"
#include "ai/DecisionProviders.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <utility>

namespace
{
Card GodsCard(const char* name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    Card c = d->card;
    c.m_number = number;
    return c;
}

void PutGodsPerm(GameState& s, const char* name, int number, bool sick = false)
{
    Permanent p;
    p.card = GodsCard(name, number);
    p.controller_index = 0;
    p.owner_index = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
}

GameState GodsBase()
{
    CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
    GameState s;
    s.active_player_index = 0;
    s.turn_number = 3;
    s.players[0].life = 20;
    s.players[1].life = 20;
    s.players[0].lands_played_this_turn = 1;
    return s;
}
}

TEST_CASE("Cephalid Coliseum threshold, payment, target, and zone order")
{
    GameState s = GodsBase();
    const CardDefinition* d = CardDatabase::Instance().Lookup("Cephalid Coliseum");
    REQUIRE(d != nullptr);
    REQUIRE(d->params.threshold_loot_cost.has_value());
    CHECK(d->params.threshold_loot_cost->ToString() == "{U}");
    CHECK(d->params.tap_self_damage == 1);
    PutGodsPerm(s, "Cephalid Coliseum", 100);
    for (int i = 0; i < 6; ++i) { s.players[0].graveyard.push_back(GodsCard("Forest", 200+i)); }
    auto offers = TurnSolver::CollectActionsForTest(s, true);
    auto count_loot = [](const std::vector<Action>& actions) {
        int n = 0;
        for (const Action& a : actions)
        { if (a.ability_mode == Action::AbilityMode::ThresholdLoot) { ++n; } }
        return n;
    };
    CHECK(count_loot(offers) == 0);
    s.players[0].graveyard.push_back(GodsCard("Mountain", 206));
    CHECK(count_loot(TurnSolver::CollectActionsForTest(s, true)) == 0); // source cannot pay its own {U}
    PutGodsPerm(s, "Island", 101);
    offers = TurnSolver::CollectActionsForTest(s, true);
    CHECK(count_loot(offers) == 2);
    bool self = false, opponent = false;
    for (const Action& a : offers)
    {
        if (a.ability_mode != Action::AbilityMode::ThresholdLoot) { continue; }
        self |= a.draw_target == 0;
        opponent |= a.draw_target == 1;
    }
    CHECK(self);
    CHECK(opponent);

    s.players[0].hand.push_back(GodsCard("Forest", 300));
    for (int i = 0; i < 4; ++i) { s.players[0].library.push_back(GodsCard("Plains", 310+i)); }
    GameState own = s;
    own.battlefield.front().tapped = true;
    CHECK(PermAbilitySourceLive(own, 0, 100, PermAbilityMode::ThresholdLoot) == false);
    own.battlefield.front().tapped = false;
    const ThresholdLootTargetScope own_target(0);
    ApplyPermAbility(own, 0, 100, PermAbilityMode::ThresholdLoot);
    CHECK(own.battlefield.size() == 1);
    CHECK(own.players[0].graveyard.size() == 11); // seven prior + land cost + three discards
    CHECK(own.players[0].graveyard[7].m_number == 100); // sacrificed before draws/discards
    CHECK(own.players[0].hand.size() == 1);
    CHECK(own.players[0].library.size() == 1);

    GameState opp = s;
    opp.opponent_library_dealt = true;
    opp.players[1].hand.push_back(GodsCard("Forest", 400));
    for (int i = 0; i < 5; ++i) { opp.players[1].library.push_back(GodsCard("Island", 410+i)); }
    {
        const ThresholdLootTargetScope target(1);
        ApplyPermAbility(opp, 0, 100, PermAbilityMode::ThresholdLoot);
    }
    CHECK(opp.players[1].library.size() == 2);
    CHECK(opp.players[1].hand.size() == 1);
    CHECK(opp.players[1].graveyard.size() == 3);
    CHECK(opp.players[0].graveyard[7].m_number == 100);
    CHECK_FALSE(opp.opponent_decked);

    // The full plan applies {T} before its {U} payment. The other Island
    // supplies blue, the Coliseum leaves as a cost, and the chosen opponent
    // target receives the draw/discard payload in the rollout.
    const auto plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* opponent_plan = nullptr;
    for (const auto& plan : plans)
    {
        if (plan.actions.size() != 1) { continue; }
        const Action& a = plan.actions.front();
        if (a.ability_mode == Action::AbilityMode::ThresholdLoot && a.draw_target == 1)
        { opponent_plan = &plan; break; }
    }
    REQUIRE(opponent_plan != nullptr);
    GameState paid = s;
    paid.opponent_library_dealt = true;
    for (int i = 0; i < 5; ++i) { paid.players[1].library.push_back(GodsCard("Island", 450+i)); }
    TurnSolver::ApplyPlan(paid, *opponent_plan, true);
    CHECK(paid.battlefield.size() == 1);
    CHECK(paid.battlefield.front().card.m_number == 101);
    CHECK(paid.battlefield.front().tapped);
    CHECK(paid.players[0].graveyard[7].m_number == 100);
    CHECK(paid.players[1].library.size() == 2);
}

TEST_CASE("Cephalid Coliseum offers dredge for each self draw")
{
    struct Chooser final : DredgeChooser
    {
        int calls = 0;
        int Choose(const GameState&, int, const std::vector<int>& options) override
        {
            ++calls;
            return options.empty() ? 0 : 1;
        }
    } chooser;
    GameState s = GodsBase();
    s.deck_has_dredge = true;
    PutGodsPerm(s, "Cephalid Coliseum", 800);
    for (int i = 0; i < 6; ++i) { s.players[0].graveyard.push_back(GodsCard("Forest", 810+i)); }
    s.players[0].graveyard.push_back(GodsCard("Stinkweed Imp", 820));
    for (int i = 0; i < 20; ++i) { s.players[0].library.push_back(GodsCard("Plains", 830+i)); }
    DredgeChooser* prior = g_dredge_chooser;
    g_dredge_chooser = &chooser;
    {
        const ThresholdLootTargetScope target(0);
        ApplyPermAbility(s, 0, 800, PermAbilityMode::ThresholdLoot);
    }
    g_dredge_chooser = prior;
    CHECK(chooser.calls >= 1);
    CHECK(s.players[0].library.size() < 17);
}

TEST_CASE("Mystic Retrieval needs a legal graveyard target in both cast modes")
{
    GameState s = GodsBase();
    const CardDefinition* d = CardDatabase::Instance().Lookup("Mystic Retrieval");
    REQUIRE(d != nullptr);
    REQUIRE(d->params.flashback_cost.has_value());
    CHECK(d->params.flashback_cost->ToString() == "{2}{R}");
    s.players[0].hand.push_back(GodsCard("Mystic Retrieval", 500));
    for (int i = 0; i < 4; ++i) { PutGodsPerm(s, "Island", 510+i); }
    for (int i = 0; i < 3; ++i) { PutGodsPerm(s, "Mountain", 520+i); }
    auto casts = [](const GameState& state) {
        int hand = 0, flashback = 0;
        for (const Action& a : TurnSolver::CollectActionsForTest(state, true))
        {
            if (a.card_name != "Mystic Retrieval") { continue; }
            hand += a.kind == Action::Kind::CastFromHand;
            flashback += a.kind == Action::Kind::CastFromGraveyard;
        }
        return std::pair<int, int>{hand, flashback};
    };
    CHECK(casts(s).first == 0);
    CHECK(casts(s).second == 0);
    s.players[0].graveyard.push_back(GodsCard("Mystic Retrieval", 501));
    CHECK_FALSE(HasGraveyardTutorTarget(s, 0, d->params, 501));
    CHECK(casts(s).first == 1);
    CHECK(casts(s).second == 0);
    s.players[0].graveyard.push_back(GodsCard("Forest", 502));
    CHECK_FALSE(HasGraveyardTutorTarget(s, 0, d->params, 501));
    s.players[0].graveyard.push_back(GodsCard("Careful Study", 503));
    CHECK(HasGraveyardTutorTarget(s, 0, d->params, 501));
    CHECK(casts(s).first == 1);
    // The graveyard copy's flashback, plus (2026-11-13, Spirits integrator B, hole B1) the hand copy's
    // PENDING flashback: 7 lands pay {3}{U} + {2}{R}, the hand cast returns one of the two targets and the
    // flashback the other -- a legal same-main line that was unfindable before. With one target only
    // (above) the pending cast is not offered.
    CHECK(casts(s).second == 2);
}

TEST_CASE("Gods Mystic Retrieval targets Replenish before dredge outlets")
{
    GameState s = GodsBase();
    const CardDefinition* retrieval = CardDatabase::Instance().Lookup("Mystic Retrieval");
    REQUIRE(retrieval != nullptr);
    s.players[0].graveyard.push_back(GodsCard("Faithless Looting", 701));
    s.players[0].graveyard.push_back(GodsCard("Careful Study", 702));
    s.players[0].graveyard.push_back(GodsCard("Replenish", 703));
    GodsProvider provider;
    CHECK(provider.TutorCandidates(s, 0, retrieval->params) == std::vector<std::string>{"Replenish"});
    s.players[0].hand.push_back(GodsCard("Replenish", 704));
    CHECK(provider.TutorCandidates(s, 0, retrieval->params) == std::vector<std::string>{"Careful Study"});
    s.players[0].graveyard.erase(s.players[0].graveyard.begin() + 1);
    CHECK(provider.TutorCandidates(s, 0, retrieval->params) == std::vector<std::string>{"Faithless Looting"});
}

TEST_CASE("Magus of the Bazaar uses two draws and three effect discards")
{
    GameState s = GodsBase();
    const CardDefinition* d = CardDatabase::Instance().Lookup("Magus of the Bazaar");
    REQUIRE(d != nullptr);
    CHECK(d->params.tap_draw_count == 2);
    CHECK(d->params.tap_post_draw_discard == 3);
    PutGodsPerm(s, "Magus of the Bazaar", 600, true);
    CHECK_FALSE(PermAbilitySourceLive(s, 0, 600, PermAbilityMode::TapDraw));
    s.battlefield.front().entered_this_turn = false;
    CHECK(PermAbilitySourceLive(s, 0, 600, PermAbilityMode::TapDraw));
    s.players[0].hand.push_back(GodsCard("Forest", 601));
    s.players[0].hand.push_back(GodsCard("Mountain", 602));
    s.players[0].library.push_back(GodsCard("Island", 603));
    s.players[0].library.push_back(GodsCard("Plains", 604));
    ApplyPermAbility(s, 0, 600, PermAbilityMode::TapDraw);
    CHECK(s.players[0].library.empty());
    CHECK(s.players[0].hand.size() == 1);
    CHECK(s.players[0].graveyard.size() == 3);

    GameState alt = GodsBase();
    PutGodsPerm(alt, "Magus of the Bazaar", 610);
    alt.players[0].hand.push_back(GodsCard("Forest", 611));
    alt.players[0].hand.push_back(GodsCard("Mountain", 612));
    alt.players[0].library.push_back(GodsCard("Island", 613));
    alt.players[0].library.push_back(GodsCard("Plains", 614));
    int searched_discard_variants = 0;
    for (const auto& plan : TurnSolver::EnumerateMainPlans(alt, true))
    {
        if (plan.putback_choice < 1 || plan.actions.size() != 1) { continue; }
        if (plan.actions.front().ability_mode == Action::AbilityMode::TapDraw
            && plan.actions.front().card_name == "Magus of the Bazaar")
        { ++searched_discard_variants; }
    }
    CHECK(searched_discard_variants > 0);
    GameState after_draw = alt;
    TrickDraw(after_draw, 0, 2);
    const int last = static_cast<int>(PutBackCandidates(after_draw, 0, 3).size()) - 1;
    REQUIRE(last > 0);
    g_scripted_putback_choice = last;
    ApplyPermAbility(alt, 0, 610, PermAbilityMode::TapDraw);
    CHECK(g_scripted_putback_choice == -1);
    CHECK(alt.players[0].hand.size() == 1);
    CHECK(alt.players[0].hand.front().m_name != s.players[0].hand.front().m_name);
}
