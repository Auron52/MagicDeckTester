// Maze's End deck, integration phase I4 (2026-11-17; docs/design/analysis-Maze's End.md): the utility Gates.
// Heap Gate (Treasure: {1}, {T}, tap ANOTHER untapped Gate), Baldur's Gate (the scaled-mana-land shape over
// the OTHER Gates we control; makes Gond Gate rainbow, CR 106.7), Basilisk Gate (+X/+X, X = our Gates at
// resolution), Talon Gates of Madara ({4}: put it from hand -- not a land play; runs first in its trailing
// pass), and the deck's Gate TAP ORDER (Guild Summit / Heap Gate's cost).
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameEngine.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
void LoadI4Cards()
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& D(const char* name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    return *d;
}

Card C(const char* name, int number)
{
    Card c = D(name).card;
    c.m_number = number;
    return c;
}

void Put(GameState& s, const char* name, int number, bool tapped = false, int controller = 0)
{
    Permanent p;
    p.card = C(name, number);
    p.controller_index = controller;
    p.owner_index = controller;
    p.tapped = tapped;
    s.battlefield.push_back(p);
}

GameState Base()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    return s;
}

const Permanent* Find(const GameState& s, int num)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == num) { return &p; } }
    return nullptr;
}

int FindIdx(const GameState& s, int num)
{
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { if (s.battlefield[static_cast<std::size_t>(i)].card.m_number == num) { return i; } }
    return -1;
}

int CountNamed(const GameState& s, const char* name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

bool Has(const std::vector<Color>& v, Color c) { return std::find(v.begin(), v.end(), c) != v.end(); }

const char* kGates[] = { "Azorius Guildgate", "Boros Guildgate", "Dimir Guildgate", "Golgari Guildgate",
                         "Gruul Guildgate", "Izzet Guildgate", "Orzhov Guildgate", "Rakdos Guildgate",
                         "Selesnya Guildgate", "Simic Guildgate" };
}   // namespace

TEST_CASE("Maze's End I4: Heap Gate's Treasure needs ANOTHER untapped Gate, taps it, and makes a Treasure")
{
    LoadI4Cards();
    static const MazesEndProvider kProv;
    GameState s = Base();
    s.m_provider = &kProv;
    Put(s, "Heap Gate", 10);
    Put(s, "Forest", 11);
    // Alone (the source cannot pay its own "Tap an untapped Gate"): not live.
    CHECK_FALSE(PermAbilitySourceLive(s, 0, 10, PermAbilityMode::TapTreasure, nullptr));
    Put(s, "Simic Guildgate", 12, /*tapped=*/true);
    CHECK_FALSE(PermAbilitySourceLive(s, 0, 10, PermAbilityMode::TapTreasure, nullptr));   // tapped: no
    Put(s, "Izzet Guildgate", 13, false, /*controller=*/1);
    CHECK_FALSE(PermAbilitySourceLive(s, 0, 10, PermAbilityMode::TapTreasure, nullptr));   // not ours: no
    Put(s, "Selesnya Guildgate", 14);
    CHECK(PermAbilitySourceLive(s, 0, 10, PermAbilityMode::TapTreasure, nullptr));
    // The pay: the OTHER Gate taps (never the source, never a non-Gate), the Treasure is created on resolution.
    s.battlefield[static_cast<std::size_t>(FindIdx(s, 10))].tapped = true;   // the {T}, paid first
    const int tapped = PayActivationOtherTap(s, 0, 10, PermAbilityMode::TapTreasure, D("Heap Gate"));
    CHECK(tapped == 14);
    CHECK(Find(s, 14)->tapped);
    CHECK_FALSE(Find(s, 11)->tapped);
    UndoActivationOtherTap(s, 0, tapped);
    CHECK_FALSE(Find(s, 14)->tapped);
    ApplyPermAbility(s, 0, 10, PermAbilityMode::TapTreasure);
    CHECK(CountNamed(s, "Treasure Token") == 1);
    // Every other mode carries no such cost (0 = nothing to pay).
    CHECK(PayActivationOtherTap(s, 0, 10, PermAbilityMode::TapDraw, D("Heap Gate")) == 0);
}

TEST_CASE("Maze's End I4: Heap Gate's Treasure is a searched plan action only when the {1} is payable elsewhere")
{
    LoadI4Cards();
    static const MazesEndProvider kProv;
    auto offered = [](const GameState& s) {
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
        {
            for (const Action& a : p.actions)
            {
                if (a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::TapTreasure)
                { return true; }
            }
        }
        return false;
    };
    GameState s = Base();
    s.m_provider = &kProv;
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < 8; ++k) { s.players[0].library.push_back(C("Forest", 100 + k)); }
    Put(s, "Heap Gate", 10);
    Put(s, "Simic Guildgate", 11);
    CHECK_FALSE(offered(s));   // Heap + the cost Gate: nothing left to pay the {1}
    Put(s, "Forest", 12);
    CHECK(offered(s));
    // Applied through the plan route (the rollout's trailing pass): Treasure made, the Gate and the Forest tapped.
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, true))
    {
        if (p.actions.size() != 1 || p.actions[0].ability_mode != Action::AbilityMode::TapTreasure) { continue; }
        GameState t = s;
        TurnSolver::ApplyPlan(t, p, true);
        CHECK(CountNamed(t, "Treasure Token") == 1);
        CHECK(Find(t, 10)->tapped);
        CHECK(Find(t, 11)->tapped);
        CHECK(Find(t, 12)->tapped);
        break;
    }
}

TEST_CASE("Maze's End I4: Baldur's Gate scales with the OTHER Gates we control and makes Gond Gate rainbow")
{
    LoadI4Cards();
    CHECK(IsScaledManaLand(D("Baldur's Gate")));
    CHECK_FALSE(IsScaledManaLand(D("Simic Guildgate")));
    GameState s = Base();
    Put(s, "Baldur's Gate", 10);
    Put(s, "Simic Guildgate", 11, /*tapped=*/true);   // tapped Gates count
    Put(s, "Izzet Guildgate", 12);
    Put(s, "Planar Nexus", 13);                        // every nonbasic land type: a Gate
    Put(s, "Forest", 14);                              // not a Gate
    Put(s, "Gruul Guildgate", 15, false, /*controller=*/1);   // not ours
    const Permanent* bg = Find(s, 10);
    CHECK(ScaledManaCreatureCount(s, bg) == 3);
    CHECK(ScaledManaCreatureCount(s) == 3);            // the null-source convention finds the same land
    // Net over the free {C}: N - 2 = 1, and the {2} feeder is payable from the untapped others.
    CHECK(ScaledManaNetYield(s, D("Baldur's Gate"), bg) == 1);
    // Gond Gate: Baldur's Gate COULD produce every colour (CR 106.7), though its `produces` is {C}.
    GameState g = Base();
    Put(g, "Gond Gate", 20);
    Put(g, "Baldur's Gate", 21);
    const std::vector<Color> cols = EffectiveProducesFor(g, 0, D("Gond Gate"), Find(g, 20));
    CHECK(cols.size() == 6);
    CHECK(Has(cols, Color::Black));
    CHECK(Has(cols, Color::White));
    // Heap Gate and Talon Gates (any-colour filters) do too.
    GameState h = Base();
    Put(h, "Gond Gate", 30);
    Put(h, "Heap Gate", 31);
    CHECK(EffectiveProducesFor(h, 0, D("Gond Gate"), Find(h, 30)).size() == 6);
    GameState t = Base();
    Put(t, "Gond Gate", 40);
    Put(t, "Talon Gates of Madara", 41);
    CHECK(EffectiveProducesFor(t, 0, D("Gond Gate"), Find(t, 40)).size() == 6);
}

TEST_CASE("Maze's End I4: Basilisk Gate pumps +X/+X, X = the Gates we control at resolution")
{
    LoadI4Cards();
    GameState s = Base();
    Put(s, "Basilisk Gate", 10);
    Put(s, "Simic Guildgate", 11, /*tapped=*/true);
    Put(s, "Planar Nexus", 12);
    Put(s, "Forest", 13);
    Put(s, "Izzet Guildgate", 14, false, /*controller=*/1);
    Put(s, "Llanowar Elves", 15);
    CHECK(TapTargetPumpSubtypeBothMultiplier(s, 0, D("Basilisk Gate")) == 3);   // itself + Simic + Nexus
    {
        const TapTargetPumpPinScope pin(15, 0);
        ApplyPermAbility(s, 0, 10, PermAbilityMode::TapTargetPump);
    }
    const Permanent* elf = Find(s, 15);
    CHECK(elf->temp_power_bonus == 3);
    CHECK(elf->temp_tough_bonus == 3);
    // Kessig Wolf Run keeps its +X/+0 (the multiplier is 1 without the param).
    CHECK(TapTargetPumpSubtypeBothMultiplier(s, 0, D("Kessig Wolf Run")) == 1);
}

TEST_CASE("Maze's End I4: Talon Gates of Madara's {4} put is a from-hand action, not a land play")
{
    LoadI4Cards();
    static const MazesEndProvider kProv;
    GameState s = Base();
    s.m_provider = &kProv;
    s.players[0].lands_played_this_turn = 1;   // the drop is spent: the put is still offered
    for (int k = 0; k < 8; ++k) { s.players[0].library.push_back(C("Forest", 100 + k)); }
    for (int k = 0; k < 4; ++k) { Put(s, "Forest", 10 + k); }
    Put(s, "Guild Summit", 20);
    s.players[0].hand.push_back(C("Talon Gates of Madara", 30));
    const TurnSolver::Plan* put_plan = nullptr;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    for (const TurnSolver::Plan& p : plans)
    {
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::Channel && a.card_name == "Talon Gates of Madara") { put_plan = &p; }
        }
    }
    REQUIRE(put_plan != nullptr);
    GameState t = s;
    TurnSolver::ApplyPlan(t, *put_plan, true);
    const Permanent* tg = Find(t, 30);
    REQUIRE(tg != nullptr);
    CHECK_FALSE(tg->tapped);                                   // enters untapped
    CHECK(t.players[0].lands_played_this_turn == 1);           // NOT a land play
    CHECK(t.players[0].hand.size() == 1);                      // Guild Summit drew for the entering Gate
    // A stranded put (the card is gone) pays nothing.
    GameState u = s;
    u.players[0].hand.clear();
    TurnSolver::ApplyPlan(u, *put_plan, true);
    for (int k = 0; k < 4; ++k) { CHECK_FALSE(Find(u, 10 + k)->tapped); }
    // The provider runs the put FIRST in its trailing pass (before Maze's End's activation).
    Action put;
    put.kind = Action::Kind::Channel;
    put.card_name = InternedName("Talon Gates of Madara");
    CHECK(kProv.ActivationOrderRankForAction(s, D("Talon Gates of Madara"), put) < 0);
}

TEST_CASE("Maze's End I4: the search puts Talon Gates from hand to reach the tenth Gate name, then wins")
{
    LoadI4Cards();
    // Eight Gate names out (untapped), Maze's End untapped, the land drop spent; Talon Gates in hand is the
    // ninth name and the library's Simic Guildgate the tenth. {4} + {3} = 7 from 8 Gates + Talon's own {C}.
    auto board = [](bool talon) {
        GameState s = Base();
        s.players[0].lands_played_this_turn = 1;
        for (int i = 0; i < 8; ++i) { Put(s, kGates[i], 800 + i); }
        Put(s, "Maze's End", 810);
        if (talon) { s.players[0].hand.push_back(C("Talon Gates of Madara", 811)); }
        s.players[0].library.push_back(C(kGates[9], 820));
        for (int k = 0; k < 12; ++k) { s.players[0].library.push_back(C("Forest", 830 + k)); }
        return s;
    };
    static const MazesEndProvider kMazeProv;
    auto play = [](GameState& s) {
        s.m_provider = &kMazeProv;
        AIEngine eng(MulliganProfile::DefaultProfile(), /*depth=*/3, /*budget_ms=*/0);
        GameEngine ge(eng);
        return ge.PlayOutFrom(s, /*max_turns=*/7, GameEngine::ResumeAt::Main1);
    };
    GameState s = board(true);
    CHECK(play(s) == 5);
    CHECK(s.won_by_effect);
    GameState c = board(false);   // control: one name short this turn
    CHECK(play(c) > 5);
}

TEST_CASE("Maze's End I4: the Gate tap order -- pure-mana Gates first, Baldur's next, a live non-mana {T} last")
{
    LoadI4Cards();
    static const MazesEndProvider kProv;
    GameState s = Base();
    Put(s, "Heap Gate", 10);
    Put(s, "Baldur's Gate", 11);
    Put(s, "Basilisk Gate", 12);   // no creature of ours: its pump is dead, so it is a plain {C} Gate
    Put(s, "Simic Guildgate", 13);
    std::vector<int> idx = { 0, 1, 2, 3 };
    kProv.EtbTapSubtypeOrder(s, 0, idx);
    REQUIRE(idx.size() == 4);   // an ORDER: nothing dropped
    CHECK(s.battlefield[static_cast<std::size_t>(idx[2])].card.m_number == 11);
    CHECK(s.battlefield[static_cast<std::size_t>(idx[3])].card.m_number == 10);
    // With a creature of ours the Basilisk's pump is live: protected with the Heap.
    Put(s, "Llanowar Elves", 14);
    std::vector<int> idx2 = { 0, 1, 2, 3 };
    kProv.EtbTapSubtypeOrder(s, 0, idx2);
    CHECK(s.battlefield[static_cast<std::size_t>(idx2[0])].card.m_number == 13);
    CHECK(s.battlefield[static_cast<std::size_t>(idx2[1])].card.m_number == 11);
    // Guild Summit's ETB uses the same order: tapping two Gates taps Simic then Baldur's, keeping the Heap.
    GameState g = Base();
    g.m_provider = &kProv;
    for (int k = 0; k < 4; ++k) { g.players[0].library.push_back(C("Forest", 100 + k)); }
    Put(g, "Heap Gate", 20);
    Put(g, "Baldur's Gate", 21);
    Put(g, "Simic Guildgate", 22);
    Put(g, "Guild Summit", 23);
    FireOwnEtbTriggers(g, 0, FindIdx(g, 23), std::string(), /*etb_kx=*/3);   // pinned count 2
    CHECK(Find(g, 22)->tapped);
    CHECK(Find(g, 21)->tapped);
    CHECK_FALSE(Find(g, 20)->tapped);
    CHECK(g.players[0].hand.size() == 2);
}
