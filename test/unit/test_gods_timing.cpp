#include <doctest/doctest.h>

#include "core/SpellEffects.h"
#include "core/HeuristicDefaults.h"
#include "ai/TurnSolver.h"
#include "ai/AIEngine.h"
#include "core/GameEngine.h"

namespace
{
const CardDefinition* Def(const char* name)
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    (void)loaded;
    return CardDatabase::Instance().Lookup(name);
}
Card Physical(const char* name, int number)
{
    const CardDefinition* d = Def(name);
    REQUIRE(d != nullptr);
    Card c = d->card;
    c.m_number = number;
    c.m_def = d;
    return c;
}
Permanent OnBoard(const char* name, int number, int controller = 0)
{
    Permanent p;
    p.card = Physical(name, number);
    p.controller_index = controller;
    p.owner_index = controller;
    return p;
}
}

TEST_CASE("Mogis deals damage only when the opponent has no creature to sacrifice")
{
    GameState s;
    s.deck_has_turn_gods = true;
    s.deck_has_devotion_creature = true;
    s.dmg_events_armed = true;
    s.battlefield.push_back(OnBoard("Mogis, God of Slaughter", 10));
    const int life = s.players[1].life;
    PerformMogisOpponentUpkeep(s);
    CHECK(s.players[1].life == life - 2);
    s.battlefield.push_back(OnBoard("Stinkweed Imp", 11, 1));
    const int after = s.players[1].life;
    PerformMogisOpponentUpkeep(s);
    CHECK(s.players[1].life == after);
    CHECK(s.players[1].graveyard.size() == 1);
}

TEST_CASE("Klothys exiles the selected physical card and searches red or green for a land")
{
    GameState base;
    base.deck_has_turn_gods = true;
    base.deck_has_devotion_creature = true;
    base.dmg_events_armed = true;
    base.battlefield.push_back(OnBoard("Klothys, God of Destiny", 20));
    Card land_from_deck;
    land_from_deck.m_name = "Reflecting Pool";
    land_from_deck.m_number = 21;
    base.players[0].graveyard.push_back(land_from_deck);
    base.players[1].graveyard.push_back(Physical("Stinkweed Imp", 22));
    GameState green = base;
    green.scripted_boundary_klothys = 1;
    PerformKlothysFirstMain(green);
    CHECK(green.floating_mana.green == 1);
    CHECK(green.floating_mana.red == 0);
    CHECK(green.exile.back().m_number == 21);
    CHECK(green.scripted_boundary_klothys == -1);
    GameState human = base;
    bool color_asked = false;
    EtbColorChooser choose_color = [&](const GameState&, int, const std::string&,
                                      const std::vector<int>&, int) {
        color_asked = true;
        return static_cast<int>(Color::Green);
    };
    TutorChooser choose_land = [](const GameState&, int, const std::string&,
                                  const std::vector<std::string>&, int,
                                  const std::vector<int>&) { return 0; };
    EtbColorChooser* previous_color = g_play_etb_color_chooser;
    TutorChooser* previous_tutor = g_play_tutor_chooser;
    g_play_etb_color_chooser = &choose_color;
    g_play_tutor_chooser = &choose_land;
    PerformKlothysFirstMain(human);
    g_play_etb_color_chooser = previous_color;
    g_play_tutor_chooser = previous_tutor;
    CHECK(color_asked);
    CHECK(human.floating_mana.green == 1);
    GameState drain = base;
    drain.scripted_boundary_klothys = 2;
    const int own_life = drain.players[0].life;
    const int opp_life = drain.players[1].life;
    PerformKlothysFirstMain(drain);
    CHECK(drain.exile.back().m_number == 22);
    CHECK(drain.players[0].life == own_life + 2);
    CHECK(drain.players[1].life == opp_life - 2);
}

TEST_CASE("Keranos waits for the first actual own-turn draw and the bonus draw does not retrigger")
{
    GameState s;
    s.deck_has_turn_gods = true;
    s.deck_has_devotion_creature = true;
    s.dmg_events_armed = true;
    s.battlefield.push_back(OnBoard("Keranos, God of Storms", 30));
    s.players[0].library.push_back(Physical("Forest", 31));
    s.players[0].library.push_back(Physical("Stinkweed Imp", 32));
    const auto first = AttemptDrawIntoHand(s, 0, HandEntryReason::Draw);
    CHECK(first.satisfied);
    CHECK(s.keranos_first_own_draw_seen);
    if (first.card_is_land)
    {
        CHECK(s.keranos_pending_land_draws == 1);
        FlushKeranosTriggers(s);
        CHECK(s.players[0].hand.size() == 2);
        CHECK(s.keranos_pending_land_draws == 0);
    }
    else
    {
        CHECK(s.keranos_pending_nonland_hits == 1);
        FlushKeranosTriggers(s);
        CHECK(s.players[1].life == 17);
    }
    CHECK(s.keranos_pending_nonland_hits == 0);
}

TEST_CASE("A dredge replacement does not use Keranos's first actual draw")
{
    CardDefinition dredger;
    dredger.params.dredge = 1;
    Card dredge_card = Physical("Stinkweed Imp", 60);
    dredge_card.m_def = &dredger;
    GameState s;
    s.deck_has_turn_gods = true;
    s.deck_has_devotion_creature = true;
    s.dmg_events_armed = true;
    s.deck_has_dredge = true;
    s.battlefield.push_back(OnBoard("Keranos, God of Storms", 61));
    s.players[0].graveyard.push_back(dredge_card);
    s.players[0].library.push_back(Physical("Forest", 62));
    s.players[0].library.push_back(Physical("Stinkweed Imp", 63));
    s.scripted_draw_dredge = 60;
    const auto replacement = AttemptDrawIntoHand(s, 0, HandEntryReason::DrawStep);
    REQUIRE(replacement.satisfied);
    CHECK_FALSE(replacement.drew);
    CHECK_FALSE(s.keranos_first_own_draw_seen);
    const auto actual = AttemptDrawIntoHand(s, 0, HandEntryReason::Draw);
    REQUIRE(actual.satisfied);
    CHECK(actual.drew);
    CHECK(s.keranos_first_own_draw_seen);
    CHECK(s.keranos_pending_nonland_hits + s.keranos_pending_land_draws == 1);
}

TEST_CASE("Ephara draws in opponent upkeep for an own-turn creature entry")
{
    GameState s;
    s.deck_has_turn_gods = true;
    s.deck_has_devotion_creature = true;
    s.dmg_events_armed = true;
    s.turn_number = 2;
    s.battlefield.push_back(OnBoard("Ephara, God of the Polis", 40));
    s.ephara_creature_entered_this_turn = true;
    for (int i = 0; i < 4; ++i) { s.players[0].library.push_back(Physical("Forest", 41 + i)); }
    REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(s));
    CHECK(s.players[0].hand.size() == 2); // opponent upkeep, then our draw step
    CHECK_FALSE(s.ephara_creature_entered_last_turn);
    CHECK_FALSE(s.ephara_creature_entered_this_turn);
}

TEST_CASE("Boundary search crosses both Keranos damage targets and Klothys card-color modes")
{
    GameState s;
    s.deck_has_turn_gods = true;
    s.deck_has_devotion_creature = true;
    s.dmg_events_armed = true;
    s.turn_number = 2;
    s.battlefield.push_back(OnBoard("Keranos, God of Storms", 50));
    s.battlefield.push_back(OnBoard("Klothys, God of Destiny", 51));
    RefreshDevotionCreatures(s);
    s.players[0].graveyard.push_back(Physical("Forest", 52));
    s.players[1].graveyard.push_back(Physical("Stinkweed Imp", 53));
    s.players[0].library.push_back(Physical("Stinkweed Imp", 54));
    s.players[0].library.push_back(Physical("Stinkweed Imp", 55));
    const auto pins = TurnSolver::GodBoundaryPinsForTest(s);
    CHECK(pins.size() == 6);
    for (int keranos = 0; keranos < 2; ++keranos)
    for (int klothys = 0; klothys < 3; ++klothys)
    { CHECK(std::find(pins.begin(), pins.end(), std::pair<int, int>{klothys, keranos}) != pins.end()); }
}

TEST_CASE("A main-phase draw spell searches Keranos's live damage target")
{
    GameState s;
    s.deck_has_turn_gods = true;
    s.deck_has_devotion_creature = true;
    s.dmg_events_armed = true;
    s.turn_number = 2;
    s.deck_has_dredge = false;
    s.players[0].lands_played_this_turn = 1;
    s.battlefield.push_back(OnBoard("Keranos, God of Storms", 70));
    s.battlefield.push_back(OnBoard("Island", 71));
    RefreshDevotionCreatures(s);
    s.players[0].hand.push_back(Physical("Careful Study", 72));
    for (int i = 0; i < 4; ++i) { s.players[0].library.push_back(Physical("Stinkweed Imp", 73 + i)); }
    const auto plans = TurnSolver::EnumerateMainPlans(s, true);
    bool face = false, self = false;
    for (const auto& p : plans)
    {
        if (p.keranos_targets == std::vector<int>{0}) { face = true; }
        if (p.keranos_targets == std::vector<int>{1}) { self = true; }
    }
    CHECK(face);
    CHECK(self);
}

TEST_CASE("Executor and rollout agree across opponent upkeep, own draw, and first main")
{
    GameState before;
    before.deck_has_turn_gods = true;
    before.deck_has_devotion_creature = true;
    before.dmg_events_armed = true;
    before.turn_number = 2;
    before.battlefield.push_back(OnBoard("Keranos, God of Storms", 80));
    before.battlefield.push_back(OnBoard("Mogis, God of Slaughter", 81));
    before.battlefield.push_back(OnBoard("Klothys, God of Destiny", 82));
    before.battlefield.push_back(OnBoard("Ephara, God of the Polis", 83));
    RefreshDevotionCreatures(before);
    before.ephara_creature_entered_this_turn = true;
    before.players[0].graveyard.push_back(Physical("Forest", 84));
    before.players[1].graveyard.push_back(Physical("Stinkweed Imp", 85));
    for (int i = 0; i < 5; ++i)
    { before.players[0].library.push_back(Physical("Stinkweed Imp", 86 + i)); }
    before.scripted_boundary_keranos = 1; // own face after the ordinary draw
    before.scripted_boundary_klothys = 2; // opponent's Imp, then drain

    GameState rollout = before;
    REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(rollout));
    GameState executor = before;
    AIEngine ai(MulliganProfile::DefaultProfile(), 0, 0);
    GameEngine engine(ai);
    REQUIRE(engine.AdvanceToFirstMainForTest(executor));
    CHECK(executor.players[0].life == rollout.players[0].life);
    CHECK(executor.players[1].life == rollout.players[1].life);
    CHECK(executor.players[0].hand.size() == rollout.players[0].hand.size());
    REQUIRE(executor.exile.size() == rollout.exile.size());
    CHECK(executor.exile.back().m_number == rollout.exile.back().m_number);
}

TEST_CASE("An extra turn skips Mogis's opponent upkeep and preserves Ephara's last-turn entry")
{
    GameState before;
    before.deck_has_turn_gods = true;
    before.deck_has_devotion_creature = true;
    before.dmg_events_armed = true;
    before.turn_number = 2;
    before.pending_extra_turns = 1;
    before.battlefield.push_back(OnBoard("Mogis, God of Slaughter", 91));
    before.battlefield.push_back(OnBoard("Ephara, God of the Polis", 92));
    RefreshDevotionCreatures(before);
    before.ephara_creature_entered_this_turn = true;
    for (int i = 0; i < 3; ++i)
    { before.players[0].library.push_back(Physical("Forest", 93 + i)); }
    GameState rollout = before;
    REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(rollout));
    GameState executor = before;
    AIEngine ai(MulliganProfile::DefaultProfile(), 0, 0);
    GameEngine engine(ai);
    REQUIRE(engine.AdvanceToFirstMainForTest(executor));
    CHECK(rollout.players[1].life == 20);
    CHECK(executor.players[1].life == rollout.players[1].life);
    CHECK(executor.players[0].hand.size() == rollout.players[0].hand.size());
    CHECK(rollout.players[0].hand.size() == 2); // extra-turn upkeep and draw step
    CHECK(executor.turn_number == rollout.turn_number);
    CHECK(executor.extra_turns_taken == rollout.extra_turns_taken);
}

TEST_CASE("Xenagos chooses a physical combat target and doubles its live power")
{
    GameState s;
    s.deck_has_devotion_creature = true;
    s.battlefield.push_back(OnBoard("Xenagos, God of Revels", 201));
    s.battlefield.push_back(OnBoard("Magus of the Bazaar", 202));
    s.battlefield.push_back(OnBoard("Stinkweed Imp", 203));
    s.battlefield[2].counters.Add(Counter{Counter::Type::PlusOnePlusOne, 2});
    s.scripted_combat_landfall_target = 1;
    FireXenagosCombat(s, 0, /*live=*/false);
    CHECK(s.battlefield[1].temp_power_bonus == 0);
    CHECK(s.battlefield[2].temp_power_bonus == 3);
    CHECK(s.battlefield[2].temp_tough_bonus == 3);
    CHECK(s.battlefield[2].temp_haste);
}

TEST_CASE("Karametra searches a physical Forest on a later creature cast and can decline")
{
    GameState s;
    s.deck_has_land_search = true;
    s.battlefield.push_back(OnBoard("Karametra, God of Harvests", 211));
    s.players[0].library.push_back(Physical("Forest", 212));
    const CardDefinition* creature = Def("Stinkweed Imp");
    REQUIRE(creature != nullptr);
    {
        const std::vector<int> decline_pin{kLandSearchPinMask};
        ScriptedLandSearch pins(decline_pin);
        FireOnCastTriggers(s, *creature);
    }
    CHECK(s.players[0].library.size() == 1);
    CHECK(s.battlefield.size() == 1);
    FireOnCastTriggers(s, *creature);
    REQUIRE(s.battlefield.size() == 2);
    CHECK(s.battlefield[1].card.m_number == 212);
    CHECK(s.battlefield[1].tapped);
}

TEST_CASE("Athreos Passage returns an owned creature only when opponent declines payment")
{
    GameState base;
    base.battlefield.push_back(OnBoard("Athreos, God of Passage", 221));
    const Card dead = Physical("Magus of the Bazaar", 222);
    base.players[0].graveyard.push_back(dead);
    GameState decline = base;
    decline.players[1].life = 2;
    OnCreatureDies(decline, 0, dead, false, 0, nullptr, 0);
    CHECK(decline.players[0].graveyard.empty());
    REQUIRE(decline.players[0].hand.size() == 1);
    CHECK(decline.players[0].hand[0].m_number == 222);
    GameState pay = base;
    OnCreatureDies(pay, 0, dead, false, 0, nullptr, 0);
    CHECK(pay.players[1].life == 17);
    CHECK(pay.players[0].hand.empty());
    CHECK(pay.players[0].graveyard.size() == 1);
}

TEST_CASE("Athreos Shroud coin survives Bronze sacrifice and returns a marked exile")
{
    GameState s;
    s.battlefield.push_back(OnBoard("Athreos, Shroud-Veiled", 231));
    s.battlefield.push_back(OnBoard("Stinkweed Imp", 232));
    s.battlefield[1].sac_at_end_step = true;
    PerformAthreosCoinEndStep(s, 0);
    REQUIRE(s.athreos_coin_ids.size() == 1);
    CHECK(s.athreos_coin_ids[0] == 232);
    PerformEndStepDelayedSacrifices(s);
    REQUIRE(s.battlefield.size() == 2);
    CHECK(s.battlefield[1].card.m_number == 232);
    CHECK(s.athreos_coin_ids.empty());
    CHECK(s.players[0].graveyard.empty());
    PerformAthreosCoinEndStep(s, 0);
    const Permanent leaving = s.battlefield[1];
    s.exile.push_back(leaving.card);
    s.battlefield.erase(s.battlefield.begin() + 1);
    ResolveAthreosExile(s, leaving);
    CHECK(s.exile.empty());
    REQUIRE(s.battlefield.size() == 2);
    CHECK(s.battlefield[1].card.m_number == 232);
}
