#include <doctest/doctest.h>

#include "core/HandEntry.h"
#include "core/SpellEffects.h"
#include "ai/TurnSolver.h"
#include "ai/DecisionProviders.h"
#include "core/HeuristicDefaults.h"
#include <cstdio>
#include <string>

namespace
{
Card MakeCard(const char* name, int number, const CardDefinition* def = nullptr)
{
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    c.m_def = def;
    return c;
}

TEST_CASE("Gods five-mana winless certificate stops at the reachable Purphoros boundary")
{
    if (!EnvOn("MTG_GODS_WINLESS_REACH5")) { return; }
    CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
    GodsProvider provider;
    GameState s;
    s.m_provider = &provider;
    s.active_player_index = 0;
    s.players[1].life = 20;
    s.players[0].lands_played_this_turn = 1; // five lands are the turn's full mana ceiling
    for (int i = 0; i < 5; ++i)
    {
        Permanent p;
        p.card = MakeCard(i == 0 ? "Cephalid Coliseum" : "Forest", i + 1,
                          CardDatabase::Instance().Lookup(i == 0 ? "Cephalid Coliseum" : "Forest"));
        p.controller_index = 0;
        s.battlefield.push_back(std::move(p));
    }
    for (int i = 0; i < 13; ++i)
    { s.players[0].library.push_back(MakeCard("Forest", 100 + i)); }
    s.players[0].library[12] = MakeCard("Purphoros, God of the Forge", 112);
    for (int i = 0; i < 20; ++i)
    { s.players[1].library.push_back(MakeCard("Forest", 200 + i)); }
    CHECK(provider.ProvenWinlessThisTurn(s, 0));
    std::swap(s.players[0].library[11], s.players[0].library[12]);
    CHECK_FALSE(provider.ProvenWinlessThisTurn(s, 0));
    std::swap(s.players[0].library[11], s.players[0].library[12]);
    s.players[1].life = 17;
    CHECK_FALSE(provider.ProvenWinlessThisTurn(s, 0));
}

TEST_CASE("Gods search policy ranks physical dredgers and post-draw discards")
{
    CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
    GodsProvider provider;
    GameState s;
    s.m_provider = &provider;
    Player& p = s.players[0];
    auto add_gy = [&](const char* name, int number)
    {
        const CardDefinition* def = CardDatabase::Instance().Lookup(name);
        REQUIRE(def != nullptr);
        // Real hand/library/graveyard entries can be name-only placeholders.
        Card c = std::string(name) == "Forest" ? Card{} : def->card;
        if (c.m_name.empty()) { c.m_name = name; c.RehashName(); }
        c.m_number = number;
        if (std::string(name) != "Forest") { c.m_def = def; }
        p.graveyard.push_back(std::move(c));
    };
    add_gy("Golgari Grave-Troll", 11);
    add_gy("Golgari Grave-Troll", 12);
    add_gy("Stinkweed Imp", 13);
    add_gy("Life from the Loam", 14);
    add_gy("Forest", 15);
    for (int i = 0; i < 12; ++i) { p.library.push_back(MakeCard("Forest", 60 + i)); }
    const std::vector<int> menu{11, 12, 13, 14};
    const int dredge_rule = std::getenv("MTG_GODS_DREDGE_RULE")
        ? std::atoi(std::getenv("MTG_GODS_DREDGE_RULE")) : 4;
    if (dredge_rule >= 4)
    {
        CHECK(provider.DredgeHumanDefault(s, 0, menu) == (dredge_rule >= 5 ? 0 : 4));
        p.hand.push_back(MakeCard("Forest", 70, CardDatabase::Instance().Lookup("Forest")));
        CHECK(provider.DredgeHumanDefault(s, 0, menu) == 1); // highest dredge with land in hand
        p.hand.clear();
    }
    const auto offered = DredgeSearchChoices(s, 0, menu);
    if (dredge_rule >= 5)
    { CHECK(offered == std::vector<int>{0}); }
    else if (dredge_rule >= 2)
    { CHECK(offered == std::vector<int>{14}); }
    else if (dredge_rule == 1)
    { CHECK(offered == std::vector<int>{0, 11, 14}); }
    else
    { CHECK(offered == std::vector<int>{0, 11, 12, 13, 14}); }
    if (dredge_rule >= 4)
    {
        CHECK(DredgeSearchSingletonPolicy(s));
        const std::vector<int> no_pins;
        std::vector<int> recorded;
        ScriptedDredge chooser(no_pins, nullptr, &recorded);
        CHECK(chooser.Choose(s, 0, menu) == (dredge_rule >= 5 ? 0 : 4));
        CHECK(recorded == std::vector<int>{dredge_rule >= 5 ? 0 : 14});
    }
    if (dredge_rule >= 5)
    {
        const CardDefinition* forest = CardDatabase::Instance().Lookup("Forest");
        REQUIRE(forest != nullptr);
        for (int i = 0; i < 2; ++i)
        {
            Permanent land;
            land.card = forest->card;
            land.card.m_number = 80 + i;
            land.controller_index = 0;
            s.battlefield.push_back(std::move(land));
        }
        CHECK(DredgeSearchChoices(s, 0, menu) == std::vector<int>{14});
        while (p.library.size() > 8) { p.library.DrawTop(); }
        CHECK(DredgeSearchChoices(s, 0, menu) == std::vector<int>{0});
        s.battlefield.clear();
        for (int i = 0; i < 4; ++i) { p.library.push_back(MakeCard("Forest", 90 + i)); }
    }

    for (const char* name : {"Replenish", "Keranos, God of Storms", "Stinkweed Imp",
                              "Golgari Grave-Troll", "Life from the Loam", "Forest"})
    {
        const CardDefinition* def = CardDatabase::Instance().Lookup(name);
        REQUIRE(def != nullptr);
        Card c = std::string(name) == "Forest" ? Card{} : def->card;
        if (c.m_name.empty()) { c.m_name = name; c.RehashName(); }
        c.m_number = static_cast<int>(p.hand.size() + 20);
        if (std::string(name) != "Forest") { c.m_def = def; }
        p.hand.push_back(std::move(c));
    }
    const auto ranking = provider.PutBackRanking(s, 0);
    if (EnvOn("MTG_GODS_DISCARD_RULE", true))
    {
        REQUIRE(ranking.size() == 6);
        CHECK(ranking[0] == 3);
        CHECK(ranking[1] == 2);
        CHECK(ranking[2] == 4);
        CHECK(ranking[3] == 1);
        CHECK(provider.PutBackSingleCandidate(s));
        const auto two = PutBackCandidates(s, 0, 2);
        REQUIRE(!two.empty());
        CHECK(two.front() == std::vector<int>{2, 3});
        const auto three = PutBackCandidates(s, 0, 3);
        REQUIRE(!three.empty());
        CHECK(three.front() == std::vector<int>{4, 2, 3});
        CHECK(PutBackFirstCandidate(s, 0, 3) == three.front());
        CHECK(PutBackCandidateCount(s, p.hand, 3) == static_cast<int>(three.size()));
        const auto shed = provider.CleanupDiscardShedOrder(s, nullptr);
        REQUIRE(shed.size() == 6);
        CHECK(shed[0] == 3);
        CHECK(shed[1] == 2);
        CHECK(shed[2] == 4);
        CHECK(shed[3] == 1);
        CHECK(provider.CleanupDiscardCandidates(s, nullptr) == std::vector<int>{3});
    }
    else { CHECK_FALSE(provider.PutBackSingleCandidate(s)); }
    if (dredge_rule >= 4)
    {
        CHECK(DredgeSearchChoices(s, 0, menu) == std::vector<int>{11});
        p.hand.clear();
        const CardDefinition* forest = CardDatabase::Instance().Lookup("Forest");
        REQUIRE(forest != nullptr);
        for (int i = 0; i < 3; ++i)
        {
            Permanent land;
            land.card = forest->card;
            land.card.m_number = 30 + i;
            land.controller_index = 0;
            s.battlefield.push_back(std::move(land));
        }
        add_gy("Keranos, God of Storms", 40);
        add_gy("Mogis, God of Slaughter", 41);
        add_gy("Nylea, God of the Hunt", 42);
        CHECK(DredgeSearchChoices(s, 0, menu)
              == std::vector<int>{dredge_rule == 5 ? 11 : 0});
        const std::vector<int> no_pins;
        std::vector<int> recorded;
        ScriptedDredge chooser(no_pins, nullptr, &recorded);
        CHECK(chooser.Choose(s, 0, menu) == (dredge_rule == 5 ? 1 : 0));
        CHECK(recorded == std::vector<int>{dredge_rule == 5 ? 11 : 0});
    }
}

TEST_CASE("Gods keeps dredgers in hand for dredging instead of casting them")
{
    CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
    GodsProvider provider;
    GameState s;
    s.m_provider = &provider;
    auto cast = [](const char* name)
    {
        Action a;
        a.kind = Action::Kind::CastFromHand;
        a.card_name = name;
        a.def = CardDatabase::Instance().Lookup(name);
        return a;
    };
    CHECK_FALSE(provider.CollectedActionAllowed(s, cast("Golgari Grave-Troll"), true));
    CHECK_FALSE(provider.CollectedActionAllowed(s, cast("Stinkweed Imp"), true));
    CHECK(provider.CollectedActionAllowed(s, cast("Life from the Loam"), true));
    CHECK(provider.CollectedActionAllowed(s, cast("Replenish"), true));
    s.turn_number = 6;
    s.players[1].life = 8;
    for (int i = 0; i < 3; ++i)
    { s.players[0].graveyard.push_back(MakeCard("Stinkweed Imp", 100 + i,
                                               CardDatabase::Instance().Lookup("Stinkweed Imp"))); }
    CHECK_FALSE(provider.CollectedActionAllowed(s, cast("Golgari Grave-Troll"), true));
    s.players[0].graveyard.push_back(MakeCard("Stinkweed Imp", 103,
                                               CardDatabase::Instance().Lookup("Stinkweed Imp")));
    CHECK(provider.CollectedActionAllowed(s, cast("Golgari Grave-Troll"), true));
}

TEST_CASE("Gods three-card discard fast default matches the full choice list")
{
    CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
    GodsProvider provider;
    const char* names[] = {"Golgari Grave-Troll", "Stinkweed Imp", "Life from the Loam",
                           "Keranos, God of Storms", "Replenish", "Forest"};
    std::uint64_t rng = 0x7e57e1ull;
    auto next = [&]() -> std::uint64_t
    { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; };
    for (int trial = 0; trial < 120; ++trial)
    {
        GameState s;
        s.m_provider = &provider;
        const int hand_size = static_cast<int>(next() % 11);
        for (int i = 0; i < hand_size; ++i)
        {
            const char* name = names[next() % 6];
            Card c = MakeCard(name, 100 + i, CardDatabase::Instance().Lookup(name));
            if (next() % 9 == 0) { c.m_is_staged = true; }
            s.players[0].hand.push_back(std::move(c));
        }
        const auto ref = PutBackCandidates(s, 0, 3);
        INFO("trial=" << trial);
        CHECK(PutBackCandidateCount(s, s.players[0].hand, 3) == static_cast<int>(ref.size()));
        CHECK(PutBackFirstCandidate(s, 0, 3)
              == (ref.empty() ? std::vector<int>{} : ref.front()));
    }
}

TEST_CASE("Gods protects the last Replenish from discard")
{
    CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
    GodsProvider provider;
    GameState s;
    s.m_provider = &provider;
    auto add = [&](const char* name, int number)
    { s.players[0].hand.push_back(MakeCard(name, number, CardDatabase::Instance().Lookup(name))); };
    add("Replenish", 1);
    add("Reflecting Pool", 2);
    add("Faithless Looting", 3);
    if (EnvOn("MTG_GODS_DISCARD_RULE", true))
    {
        CHECK(provider.PutBackRanking(s, 0).back() == 0);
        CHECK(provider.CleanupDiscardShedOrder(s, nullptr).back() == 0);
        CHECK(provider.CleanupDiscardCandidates(s, nullptr).front() != 0);
        add("Replenish", 4);
        const auto duplicates = provider.CleanupDiscardShedOrder(s, nullptr);
        CHECK(duplicates[0] == 3);
        CHECK(duplicates.back() == 0);
        const auto looting_pick = PutBackCandidates(s, 0, 2).front();
        REQUIRE(looting_pick.size() == 2);
        CHECK(std::find(looting_pick.begin(), looting_pick.end(), 2) != looting_pick.end());
        CHECK(std::count_if(looting_pick.begin(), looting_pick.end(),
                            [](int i) { return i == 0 || i == 3; }) == 1);
        const auto fast_pick = PutBackPickAt(s, 0, 2, 0);
        CHECK(fast_pick.idx[0] == looting_pick[0]);
        CHECK(fast_pick.idx[1] == looting_pick[1]);
        const auto magus_pick = PutBackFirstCandidate(s, 0, 3);
        REQUIRE(magus_pick.size() == 3);
        CHECK(std::count_if(magus_pick.begin(), magus_pick.end(), [](int i) { return i == 0 || i == 3; }) == 1);
        CHECK(magus_pick == PutBackCandidates(s, 0, 3).front());
        s.players[0].hand.pop_back();
        CHECK(provider.CleanupDiscardShedOrder(s, nullptr).back() == 0);
    }
}

TEST_CASE("Gods discard order prefers dredge until ten Gods and a payoff are assembled")
{
    CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
    GodsProvider provider;
    GameState s;
    s.m_provider = &provider;
    const char* cards[] = {"Keranos, God of Storms", "Stinkweed Imp", "Life from the Loam",
                           "Golgari Grave-Troll", "Replenish"};
    for (int i = 0; i < 5; ++i)
    { s.players[0].hand.push_back(MakeCard(cards[i], i + 1, CardDatabase::Instance().Lookup(cards[i]))); }
    if (EnvOn("MTG_GODS_DISCARD_RULE", true))
    {
        CHECK(provider.PutBackRanking(s, 0) == std::vector<int>{3, 1, 2, 0, 4});
        const char* more[] = {"Mogis, God of Slaughter", "Nylea, God of the Hunt",
            "Phenax, God of Deception", "Xenagos, God of Revels", "Iroas, God of Victory",
            "Pharika, God of Affliction", "Klothys, God of Destiny",
            "Kruphix, God of Horizons", "Karametra, God of Harvests"};
        for (int i = 0; i < 9; ++i)
        { s.players[0].graveyard.push_back(MakeCard(more[i], 20 + i, CardDatabase::Instance().Lookup(more[i]))); }
        CHECK(provider.PutBackRanking(s, 0) == std::vector<int>{3, 1, 2, 0, 4});
        s.players[0].graveyard.back() = MakeCard("Purphoros, God of the Forge", 28,
            CardDatabase::Instance().Lookup("Purphoros, God of the Forge"));
        CHECK(provider.PutBackRanking(s, 0) == std::vector<int>{0, 3, 1, 2, 4});
    }
}

TEST_CASE("Upkeep Magus discards Grave-Troll before the draw step")
{
    CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
    GodsProvider provider;
    GameState s;
    s.m_provider = &provider;
    s.turn_number = 2;
    s.deck_has_dredge = true;
    auto real = [](const char* name, int number)
    {
        const CardDefinition* def = CardDatabase::Instance().Lookup(name);
        Card c = def->card;
        c.m_def = def;
        c.m_number = number;
        return c;
    };
    Permanent magus;
    magus.card = real("Magus of the Bazaar", 10);
    magus.controller_index = 0;
    s.battlefield.push_back(magus);
    Player& p = s.players[0];
    p.hand.push_back(real("Golgari Grave-Troll", 11));
    p.hand.push_back(real("Keranos, God of Storms", 12));
    p.hand.push_back(real("Replenish", 13));
    for (int i = 0; i < 8; ++i) { p.library.push_back(real("Forest", 20 + i)); }

    PerformProviderUpkeepTapDraw(s);
    if (EnvOn("MTG_GODS_MAGUS_UPKEEP", true) && EnvOn("MTG_GODS_DISCARD_RULE", true))
    {
        CHECK(s.battlefield[0].tapped);
        CHECK(p.library.size() == 6);
        if (EnvOn("MTG_GODS_DISCARD_RULE", true))
        {
            CHECK(std::any_of(p.graveyard.begin(), p.graveyard.end(),
                              [](const Card& c) { return c.m_number == 11; }));
            struct PickTroll final : DredgeChooser
            {
                int Choose(const GameState&, int, const std::vector<int>&) override { return 1; }
            } picks;
            DredgeChooser* previous = g_dredge_chooser;
            g_dredge_chooser = &picks;
            const DrawAttemptResult result = AttemptDrawIntoHand(s, 0, HandEntryReason::DrawStep);
            g_dredge_chooser = previous;
            CHECK(result.satisfied);
            CHECK_FALSE(result.drew);
            CHECK(result.card_number == 11);
        }
    }
    else
    {
        CHECK_FALSE(s.battlefield[0].tapped);
        CHECK(p.graveyard.empty());
        CHECK(p.library.size() == 8);
    }
}

TEST_CASE("Viewer can choose Magus activation in upkeep before the turn draw")
{
    // Run this case with MTG_HUMAN_PLAY=1; autonomous test runs do not install viewer hooks.
    if (!HumanPlayActive()) { return; }
    CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
    GodsProvider provider;
    GameState s;
    s.m_provider = &provider;
    s.turn_number = 3;
    s.deck_has_dredge = true;
    auto real = [](const char* name, int number)
    {
        const CardDefinition* def = CardDatabase::Instance().Lookup(name);
        Card c = def->card; c.m_def = def; c.m_number = number; return c;
    };
    Permanent magus;
    magus.card = real("Magus of the Bazaar", 10);
    magus.controller_index = 0;
    s.battlefield.push_back(magus);
    Player& p = s.players[0];
    p.hand.push_back(real("Golgari Grave-Troll", 11));
    p.hand.push_back(real("Replenish", 12));
    p.hand.push_back(real("Forest", 13));
    for (int i = 0; i < 8; ++i) { p.library.push_back(real("Forest", 20+i)); }
    const bool old_prompt = g_play_magus_upkeep_prompt;
    const bool old_hooks = g_play_hooks_installed;
    TutorChooser* old_chooser = g_play_tutor_chooser;
    g_play_magus_upkeep_prompt = true;
    g_play_hooks_installed = true;
    int pick = -1, asked = 0;
    TutorChooser chooser = [&](const GameState&, int, const std::string& source,
                               const std::vector<std::string>& choices, int, const std::vector<int>&)
    {
        CHECK(source == "Magus of the Bazaar (upkeep ability)");
        CHECK(choices == std::vector<std::string>{"Activate"});
        ++asked;
        return pick;
    };
    g_play_tutor_chooser = &chooser;
    PerformProviderUpkeepTapDraw(s);
    CHECK(asked == 1);
    CHECK_FALSE(s.battlefield[0].tapped);
    CHECK(p.library.size() == 8);
    pick = 0;
    PerformProviderUpkeepTapDraw(s);
    CHECK(asked == 2);
    CHECK(s.battlefield[0].tapped);
    CHECK(p.library.size() == 6);
    CHECK(p.graveyard.size() == 3);
    g_play_tutor_chooser = old_chooser;
    g_play_hooks_installed = old_hooks;
    g_play_magus_upkeep_prompt = old_prompt;
}

struct Picks final : DredgeChooser
{
    std::vector<int> picks;
    std::vector<std::vector<int>> offered;
    int Choose(const GameState&, int, const std::vector<int>& numbers) override
    {
        offered.push_back(numbers);
        const int pick = picks.front();
        picks.erase(picks.begin());
        return pick;
    }
};

struct Draws final : ActualDrawObserver
{
    std::vector<bool> first;
    void OnActualDraw(GameState&, int, const Card&, bool is_first) override
    { first.push_back(is_first); }
};
}

TEST_CASE("Dredge replaces each draw independently and reveals later options")
{
    CardDefinition dredge_two, dredge_one;
    dredge_two.params.dredge = 2;
    dredge_one.params.dredge = 1;
    GameState s;
    Player& p = s.players[0];
    p.graveyard.push_back(MakeCard("Dredge Two", 1, &dredge_two));
    p.library.push_back(MakeCard("Dredge One", 2, &dredge_one));
    p.library.push_back(MakeCard("Forest", 3));
    p.library.push_back(MakeCard("Plains", 4));
    Picks picks;
    picks.picks = {1, 1};
    DredgeChooser* saved = g_dredge_chooser;
    g_dredge_chooser = &picks;
    const DrawBatchResult result = DrawIntoHandDetailed(s, 0, 2, HandEntryReason::Draw);
    g_dredge_chooser = saved;

    CHECK(result.satisfied == 2);
    CHECK(result.drawn == 0);
    REQUIRE(picks.offered.size() == 2);
    CHECK(picks.offered[0] == std::vector<int>{1});
    CHECK(picks.offered[1] == std::vector<int>{2});
    REQUIRE(p.hand.size() == 2);
    CHECK(p.hand[0].m_number == 1);
    CHECK(p.hand[1].m_number == 2);
    CHECK(p.graveyard.size() == 2);
    CHECK(p.graveyard[0].m_number == 3);
    CHECK(p.graveyard[1].m_number == 4);
    CHECK(p.library.empty());
}

TEST_CASE("Actual-draw observer excludes dredge and identifies first real draw in a batch")
{
    CardDefinition dredge_one;
    dredge_one.params.dredge = 1;
    GameState s;
    Player& p = s.players[0];
    p.graveyard.push_back(MakeCard("Dredge One", 1, &dredge_one));
    p.library.push_back(MakeCard("Forest", 2));
    p.library.push_back(MakeCard("Plains", 3));
    p.library.push_back(MakeCard("Island", 4));
    Picks picks;
    picks.picks = {1, 0, 0};
    Draws observer;
    DredgeChooser* saved_pick = g_dredge_chooser;
    ActualDrawObserver* saved_observer = g_actual_draw_observer;
    g_dredge_chooser = &picks;
    g_actual_draw_observer = &observer;
    std::vector<std::pair<int, InternedName>> actual;
    const DrawBatchResult result = DrawIntoHandDetailed(s, 0, 3, HandEntryReason::Draw, &actual);
    g_dredge_chooser = saved_pick;
    g_actual_draw_observer = saved_observer;

    CHECK(result.satisfied == 3);
    CHECK(result.drawn == 2);
    CHECK(observer.first == std::vector<bool>{true, false});
    REQUIRE(actual.size() == 2);
    CHECK(actual[0].first == 3);
    CHECK(actual[1].first == 4);
    CHECK(p.hand.size() == 3);
}

TEST_CASE("An attempted draw from an empty library is unsatisfied")
{
    GameState s;
    const DrawBatchResult result = DrawIntoHandDetailed(s, 0, 1, HandEntryReason::DrawStep);
    CHECK(result.satisfied == 0);
    CHECK(result.drawn == 0);
    CHECK(s.players[0].hand.empty());
}

TEST_CASE("Life from the Loam returns selected physical land targets only")
{
    CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
    const CardDefinition* forest = CardDatabase::Instance().Lookup("Forest");
    const CardDefinition* island = CardDatabase::Instance().Lookup("Island");
    REQUIRE(forest != nullptr);
    REQUIRE(island != nullptr);
    GameState s;
    auto& gy = s.players[0].graveyard;
    gy.push_back(MakeCard("Forest", 11, forest));
    gy.push_back(MakeCard("Forest", 12, forest));
    gy.push_back(MakeCard("Island", 13, island));
    ReturnTargetedGraveyardLands(s, 0, "L", 3);
    CHECK(gy.size() == 3);
    ReturnTargetedGraveyardLands(s, 0, "L11,13", 3);
    REQUIRE(s.players[0].hand.size() == 2);
    CHECK(s.players[0].hand[0].m_number == 11);
    CHECK(s.players[0].hand[1].m_number == 13);
    CHECK(gy.size() == 1);
    CHECK(gy[0].m_number == 12);
    ReturnTargetedGraveyardLands(s, 0, "L12,99", 3); // one target disappeared
    CHECK(s.players[0].hand.size() == 3);
    CHECK(gy.empty());
}

TEST_CASE("Grave-Troll enters with doubled counters from other graveyard creatures")
{
    CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
    const CardDefinition* troll = CardDatabase::Instance().Lookup("Golgari Grave-Troll");
    const CardDefinition* imp = CardDatabase::Instance().Lookup("Stinkweed Imp");
    REQUIRE(troll != nullptr);
    REQUIRE(imp != nullptr);
    GameState s;
    s.players[0].graveyard.push_back(MakeCard("Stinkweed Imp", 11, imp));
    Permanent p;
    p.card = troll->card;
    p.card.m_number = 12;
    p.card.m_def = troll;
    p.controller_index = 0;
    p.owner_index = 0;
    s.battlefield.push_back(p);
    FireEtbWatchers(s, 0, 0);
    CHECK(PlusCountersOn(s.battlefield[0]) == 1);
    CHECK(GraveyardCreatureCount(s, 0) == 1);

    SUBCASE("counter doubler applies to the as-enters replacement")
    {
        const CardDefinition* season = CardDatabase::Instance().Lookup("Doubling Season");
        REQUIRE(season != nullptr);
        GameState doubled;
        doubled.deck_has_counter_doubler = true;
        doubled.players[0].graveyard.push_back(MakeCard("Stinkweed Imp", 21, imp));
        Permanent d;
        d.card = MakeCard("Doubling Season", 22, season);
        d.controller_index = 0;
        d.owner_index = 0;
        doubled.battlefield.push_back(d);
        p.card.m_number = 23;
        doubled.battlefield.push_back(p);
        FireEtbWatchers(doubled, 0, 1);
        CHECK(PlusCountersOn(doubled.battlefield[1]) == 2);
    }
    SUBCASE("zero creatures means the printed 0/0 dies to a state action")
    {
        GameState empty;
        empty.battlefield.push_back(p);
        FireEtbWatchers(empty, 0, 0);
        DrainPendingZeroToughness(empty);
        CHECK(empty.battlefield.empty());
    }
}

TEST_CASE("Life from the Loam exposes every physical land target subset to search")
{
    CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
    const CardDefinition* loam = CardDatabase::Instance().Lookup("Life from the Loam");
    const CardDefinition* forest = CardDatabase::Instance().Lookup("Forest");
    REQUIRE(loam != nullptr);
    REQUIRE(forest != nullptr);
    GameState s;
    s.turn_number = 2;
    s.players[0].lands_played_this_turn = 1;
    for (int i = 0; i < 2; ++i)
    {
        Permanent mana;
        mana.card = MakeCard("Forest", 20 + i, forest);
        mana.controller_index = 0;
        mana.owner_index = 0;
        s.battlefield.push_back(mana);
    }
    s.players[0].hand.push_back(MakeCard("Life from the Loam", 30, loam));
    for (int i = 0; i < 3; ++i)
    { s.players[0].graveyard.push_back(MakeCard("Forest", 40 + i, forest)); }
    const auto plans = TurnSolver::EnumerateMainPlans(s, true);
    std::vector<std::string> targets;
    for (const auto& p : plans)
    {
        if (p.actions.size() == 1 && p.actions[0].card_name == "Life from the Loam")
        { targets.push_back(p.actions[0].tutor_target.str()); }
    }
    std::sort(targets.begin(), targets.end());
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    CHECK(targets.size() == 8); // sum C(3, k), k = 0..3
    CHECK(std::find(targets.begin(), targets.end(), "L40,41,42") != targets.end());
}

TEST_CASE("Life from the Loam target declaration sees earlier Mine sacrifice, not its own payment")
{
    CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
    const CardDefinition* loam = CardDatabase::Instance().Lookup("Life from the Loam");
    const CardDefinition* mine = CardDatabase::Instance().Lookup("Gemstone Mine");
    const CardDefinition* forest = CardDatabase::Instance().Lookup("Forest");
    const CardDefinition* bolt = CardDatabase::Instance().Lookup("Lightning Bolt");
    REQUIRE(loam != nullptr);
    REQUIRE(mine != nullptr);
    REQUIRE(forest != nullptr);
    REQUIRE(bolt != nullptr);
    auto base = [&]()
    {
        GameState s;
        s.turn_number = 2;
        s.players[0].lands_played_this_turn = 1;
        Permanent m;
        m.card = mine->card; m.card.m_number = 70; m.card.m_def = mine;
        m.controller_index = 0; m.owner_index = 0; m.charge_counters = 1;
        s.battlefield.push_back(m);
        for (int i = 0; i < 2; ++i)
        {
            Permanent f;
            f.card = forest->card; f.card.m_number = 71 + i; f.card.m_def = forest;
            f.controller_index = 0; f.owner_index = 0;
            s.battlefield.push_back(f);
        }
        s.players[0].hand.push_back(MakeCard("Life from the Loam", 80, loam));
        return s;
    };
    SUBCASE("Mine cannot be targeted when it pays for Loam itself")
    {
        GameState s = base();
        s.battlefield.pop_back(); // only Mine + one Forest: Mine must pay generic
        const auto plans = TurnSolver::EnumerateMainPlans(s, true);
        for (const auto& p : plans) for (const auto& a : p.actions)
        { if (a.card_name == "Life from the Loam") { CHECK(a.tutor_target == "L"); } }
    }
    SUBCASE("Mine sacrificed for an earlier red cast can be targeted later")
    {
        GameState s = base();
        s.players[0].hand.push_back(MakeCard("Lightning Bolt", 81, bolt));
        const auto plans = TurnSolver::EnumerateMainPlans(s, true);
        bool found = false;
        for (const auto& p : plans)
        {
            bool has_bolt = false, loam_targets_mine = false;
            for (const auto& a : p.actions)
            {
                if (a.card_name == "Lightning Bolt") { has_bolt = true; }
                if (a.card_name == "Life from the Loam" && a.tutor_target == "L70")
                { loam_targets_mine = true; }
            }
            if (has_bolt && loam_targets_mine) { found = true; break; }
        }
        CHECK(found);
    }
}

TEST_CASE("Searched Careful Study branches on a dredger revealed by its first replacement")
{
    CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
    const CardDefinition* study = CardDatabase::Instance().Lookup("Careful Study");
    const CardDefinition* island = CardDatabase::Instance().Lookup("Island");
    REQUIRE(study != nullptr);
    REQUIRE(island != nullptr);
    CardDefinition dredge_two, dredge_one;
    dredge_two.params.dredge = 2;
    dredge_one.params.dredge = 1;

    GameState s;
    s.turn_number = 2;
    s.players[0].lands_played_this_turn = 1;
    Permanent mana;
    mana.card = MakeCard("Island", 1, island);
    mana.controller_index = 0;
    mana.owner_index = 0;
    s.battlefield.push_back(mana);
    s.players[0].hand.push_back(MakeCard("Careful Study", 2, study));
    s.players[0].graveyard.push_back(MakeCard("Dredge Two", 3, &dredge_two));
    s.players[0].library.push_back(MakeCard("Dredge One", 4, &dredge_one));
    s.players[0].library.push_back(MakeCard("Forest", 5));
    s.players[0].library.push_back(MakeCard("Plains", 6));

    const auto plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* adaptive = nullptr;
    for (const auto& p : plans)
    {
        if (p.actions.size() == 1 && p.actions[0].card_name == "Careful Study"
            && p.dredge_pins == std::vector<int>{3, 4})
        { adaptive = &p; break; }
    }
    REQUIRE(adaptive != nullptr);
    GameState after = s;
    TurnSolver::ApplyPlan(after, *adaptive, true);
    CHECK(after.players[0].library.empty());
    CHECK(after.players[0].cards_drawn_this_turn == 0);
    CHECK(after.players[0].graveyard.size() >= 2); // milled cards plus Study/discards
}

TEST_CASE("Next-turn boundary searches normal draw and physical dredger")
{
    CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
    CardDefinition dredge_one;
    dredge_one.params.dredge = 1;
    GameState s;
    s.turn_number = 2;
    s.players[0].graveyard.push_back(MakeCard("Dredge One", 3, &dredge_one));
    s.players[0].library.push_back(MakeCard("Forest", 4));
    s.players[0].library.push_back(MakeCard("Plains", 5));
    s.players[0].library.push_back(MakeCard("Island", 6));
    CHECK(TurnSolver::DredgeBoundaryPinsForTest(s) == std::vector<int>{0, 3});

    s.scripted_draw_dredge = 3;
    const DrawAttemptResult draw = AttemptDrawIntoHand(s, 0, HandEntryReason::DrawStep);
    CHECK(draw.satisfied);
    CHECK_FALSE(draw.drew);
    CHECK(draw.card_number == 3);
    CHECK(s.scripted_draw_dredge == -1);
    REQUIRE(s.players[0].hand.size() == 1);
    CHECK(s.players[0].hand.front().m_number == 3);
}

TEST_CASE("Boundary dredge sequence adapts from upkeep draw to draw step")
{
    CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
    const CardDefinition* visitor = CardDatabase::Instance().Lookup("Asylum Visitor");
    REQUIRE(visitor != nullptr);
    CardDefinition dredge_two, dredge_one;
    dredge_two.params.dredge = 2;
    dredge_one.params.dredge = 1;
    GameState s;
    s.turn_number = 2;
    Permanent v;
    v.card = MakeCard("Asylum Visitor", 20, visitor);
    v.controller_index = 0;
    v.owner_index = 0;
    s.battlefield.push_back(v);
    s.players[0].graveyard.push_back(MakeCard("Dredge Two", 21, &dredge_two));
    s.players[0].library.push_back(MakeCard("Dredge One", 22, &dredge_one));
    s.players[0].library.push_back(MakeCard("Forest", 23));
    s.players[0].library.push_back(MakeCard("Island", 24));
    s.players[0].library.push_back(MakeCard("Plains", 25));
    const auto pins = TurnSolver::DredgeBoundaryPinSequencesForTest(s);
    CHECK(std::find(pins.begin(), pins.end(), std::vector<int>{21, 22}) != pins.end());
    CHECK(std::find(pins.begin(), pins.end(), std::vector<int>{0, 21}) != pins.end());
}

TEST_CASE("Dredge choices remain available in a depth-one Gods draw search")
{
    CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
    const CardDefinition* study = CardDatabase::Instance().Lookup("Careful Study");
    const CardDefinition* island = CardDatabase::Instance().Lookup("Island");
    REQUIRE(study != nullptr);
    REQUIRE(island != nullptr);
    CardDefinition dredge_two, dredge_one;
    dredge_two.params.dredge = 2;
    dredge_one.params.dredge = 1;
    GameState s;
    s.turn_number = 2;
    s.players[0].lands_played_this_turn = 1;
    Permanent mana;
    mana.card = MakeCard("Island", 1, island);
    mana.controller_index = 0;
    mana.owner_index = 0;
    s.battlefield.push_back(mana);
    s.players[0].hand.push_back(MakeCard("Careful Study", 2, study));
    s.players[0].graveyard.push_back(MakeCard("Dredge Two", 3, &dredge_two));
    s.players[0].library.push_back(MakeCard("Dredge One", 4, &dredge_one));
    s.players[0].library.push_back(MakeCard("Forest", 5));
    s.players[0].library.push_back(MakeCard("Plains", 6));
    const auto plans = TurnSolver::EnumerateMainPlans(s, true);
    CHECK(plans.size() > 2);
    SearchBudget budget = SearchBudget::FromVirtualMs(1);
    const TurnSolver::Plan pick = TurnSolver::SolveWithLookahead(
        s, true, 1, 3, &budget, true, false, nullptr);
    const bool valid_pin_width = pick.dredge_pins.empty() || pick.dredge_pins.size() <= 2;
    CHECK(valid_pin_width);
    std::fprintf(stderr, "[gods-dredge-depth1] plans=%zu budget_used=%lld\n",
                 plans.size(), budget.Used());
}

// MTG_LAND_AFTER_DREDGE_DRAW (mana-clean ENUM-GAP, Gods s30005620 gi5620 T8): in a deck that can dredge, the land
// drop is also offered right AFTER a draw cast ("Careful Study, then the land, then Life from the Loam on what the
// dredge milled"). The continuation cannot carry that line -- the land and Loam are old cards, so MTG_BP_NEW_ONLY
// drops it -- and the land-first plan reads the dredge answer with the land already in play.
TEST_CASE("Dredge deck offers the land drop right after a draw cast (land_slot after Careful Study)")
{
    CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
    GodsProvider provider;
    auto card = [](const char* name, int number)
    {
        const CardDefinition* d = CardDatabase::Instance().Lookup(name);
        REQUIRE(d != nullptr);
        Card c = d->card;
        c.m_number = number;
        return c;
    };
    auto build = [&](bool dredge_deck)
    {
        GameState s;
        s.m_provider = &provider;
        s.active_player_index = 0;
        s.turn_number = 4;
        s.players[0].life = 20;
        s.players[1].life = 20;
        s.deck_has_landfall     = false;
        s.deck_has_scry_watcher = false;
        s.deck_has_dredge       = dredge_deck;
        for (int i = 0; i < 2; ++i)
        {
            Permanent land;
            land.card = card(i == 0 ? "City of Brass" : "Forest", 1 + i);
            land.controller_index = 0;
            land.owner_index = 0;
            s.battlefield.push_back(land);
        }
        s.players[0].hand.push_back(card("Careful Study", 10));
        s.players[0].hand.push_back(card("Life from the Loam", 11));
        s.players[0].hand.push_back(card("Forest", 12));
        s.players[0].hand.push_back(card("Phenax, God of Deception", 13));   // the discards' fodder
        s.players[0].hand.push_back(card("Kruphix, God of Horizons", 14));
        s.players[0].graveyard.push_back(card("Golgari Grave-Troll", 20));
        for (int i = 0; i < 16; ++i) { s.players[0].library.push_back(card("Forest", 60 + i)); }
        for (int i = 0; i < 20; ++i) { s.players[1].library.push_back(card("Forest", 200 + i)); }
        return s;
    };
    // A plan playing the Forest right after Careful Study, then Loam.
    const auto positioned = [](const std::vector<TurnSolver::Plan>& plans) -> const TurnSolver::Plan*
    {
        for (const TurnSolver::Plan& p : plans)
        {
            if (p.land_to_play != "Forest" || p.land_slot != 1 || !p.searched_order || p.actions.size() != 2) { continue; }
            if (p.actions[0].card_name == "Careful Study" && p.actions[1].card_name == "Life from the Loam") { return &p; }
        }
        return nullptr;
    };

    GameState s = build(true);
    Action cast;
    cast.kind = Action::Kind::CastFromHand;
    cast.card_name = "Careful Study";
    CHECK(TurnSolver::LandSlotCounts(s, cast));
    CHECK(TurnSolver::LandPositionAxisLive(s));
    const auto plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* p = positioned(plans);
    REQUIRE(p != nullptr);
    CHECK_FALSE(p->dredge_pins.empty());   // Careful Study's first draw dredges the Grave-Troll (land still in hand)
    GameState after = s;
    TurnSolver::ApplyPlan(after, *p, true);
    int lands = 0;
    for (const Permanent& q : after.battlefield) { lands += q.controller_index == 0 && q.card.IsLand(); }
    CHECK(lands == 3);
    bool loam_in_gy = false;
    for (const Card& c : after.players[0].graveyard) { loam_in_gy |= c.m_name == "Life from the Loam"; }
    CHECK(loam_in_gy);   // cast, after the land paid for it

    // A deck with no dredge card counts the watchers only: no position after a draw.
    GameState plain = build(false);
    CHECK_FALSE(TurnSolver::LandSlotCounts(plain, cast));
    CHECK_FALSE(TurnSolver::LandPositionAxisLive(plain));
    CHECK(positioned(TurnSolver::EnumerateMainPlans(plain, true)) == nullptr);
}
