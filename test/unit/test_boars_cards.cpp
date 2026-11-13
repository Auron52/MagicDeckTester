// Boars I1 (2026-11-13): Windswept Heath's battlefield model, Flinthoof Boar (static Mountain pump + the {R}
// self-haste activation, ActivatePump mode 6), Herald's Horn (type choice + reduction + the upkeep reveal with
// its take fold / searched residual), the `creature_type` human decision and BoarsProvider routing.
#include <doctest/doctest.h>
#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameLogger.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include <string>
#include <vector>

namespace
{
const CardDefinition& BDef(const char* name)
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
Card BCard(const char* name, int number)
{
    Card c = BDef(name).card;
    c.m_number = number;
    return c;
}
Permanent& BPut(GameState& s, const char* name, int number, bool sick = false)
{
    Permanent p;
    p.card = BCard(name, number);
    p.controller_index = p.owner_index = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}
GameState BBase()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    return s;
}
const Permanent* BNum(const GameState& s, int num)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == num) { return &p; } }
    return nullptr;
}
bool OffersSelfHaste(const GameState& s, int src, bool pre_combat)
{
    for (const Action& a : TurnSolver::CollectActionsForTest(s, pre_combat))
    {
        if (a.kind == Action::Kind::ActivatePump && a.gy_exile_mode == 6 && a.sac_source_id == src) { return true; }
    }
    return false;
}
}   // namespace

TEST_CASE("Windswept Heath is a real battlefield fetchland (the Wooded Foothills shape)")
{
    const CardDefinition& h = BDef("Windswept Heath");
    const CardDefinition& f = BDef("Wooded Foothills");
    CHECK(h.params.produces.empty());
    CHECK(h.params.fetch_land_types == std::vector<std::string>{ "Forest", "Plains" });
    CHECK(h.params.land_search_count == f.params.land_search_count);
    CHECK(h.params.land_search_activation_taps == f.params.land_search_activation_taps);
    CHECK(h.params.land_search_activation_sacrifice_self == f.params.land_search_activation_sacrifice_self);
    CHECK(h.params.land_search_activation_life_cost == 1);
}

TEST_CASE("Flinthoof Boar gets +1/+1 while you control a Mountain (by current subtype), once")
{
    GameState s = BBase();
    BPut(s, "Forest", 10);
    Permanent& boar = BPut(s, "Flinthoof Boar", 1);
    const Card boar_card = boar.card;
    auto bonus = [&]() { return ComputeLordBonus(boar_card, s, 0, false, BNum(s, 1)); };
    CHECK(bonus() == std::pair<int, int>{ 0, 0 });
    BPut(s, "Stomping Ground", 11);                       // Mountain Forest
    CHECK(bonus() == std::pair<int, int>{ 1, 1 });
    BPut(s, "Mountain", 12);                              // a second Mountain: still ONE bonus
    CHECK(bonus() == std::pair<int, int>{ 1, 1 });
    // An OPPONENT's Mountain does not count.
    GameState t = BBase();
    BPut(t, "Flinthoof Boar", 1);
    Permanent opp;
    opp.card = BCard("Mountain", 50);
    opp.controller_index = opp.owner_index = 1;
    t.battlefield.push_back(opp);
    CHECK(ComputeLordBonus(BNum(t, 1)->card, t, 0, false, BNum(t, 1)) == std::pair<int, int>{ 0, 0 });
}

TEST_CASE("Kumena's Speaker keeps its either-condition self pump (the gate fix is byte-identical for it)")
{
    GameState s = BBase();
    BPut(s, "Kumena's Speaker", 1);
    auto bonus = [&]() { return ComputeLordBonus(BNum(s, 1)->card, s, 0, false, BNum(s, 1)); };
    CHECK(bonus() == std::pair<int, int>{ 0, 0 });
    BPut(s, "Forest", 10);
    CHECK(bonus() == std::pair<int, int>{ 0, 0 });
    BPut(s, "Island", 11);
    CHECK(bonus() == std::pair<int, int>{ 1, 1 });
    GameState t = BBase();
    BPut(t, "Kumena's Speaker", 1);
    BPut(t, "Kumena's Speaker", 2);                       // ANOTHER Merfolk
    CHECK(ComputeLordBonus(BNum(t, 1)->card, t, 0, false, BNum(t, 1)) == std::pair<int, int>{ 1, 1 });
}

TEST_CASE("Flinthoof's {R} haste is offered once, pre-combat, only while it is summoning-sick")
{
    const CardDefinition& d = BDef("Flinthoof Boar");
    REQUIRE(d.params.self_haste_cost.has_value());
    CHECK(d.params.self_haste_cost->ToString() == "{R}");
    CHECK_FALSE(d.params.team_pump_grants_haste);          // not the Minotaur routing signature

    GameState s = BBase();
    BPut(s, "Mountain", 10);
    BPut(s, "Flinthoof Boar", 1, /*sick=*/true);
    CHECK(SelfHasteActivationLive(s, 0, 1));
    CHECK(OffersSelfHaste(s, 1, /*pre_combat=*/true));
    CHECK_FALSE(OffersSelfHaste(s, 1, /*pre_combat=*/false));   // haste after combat lifts nothing
    int offered = 0;
    for (const Action& a : TurnSolver::CollectActionsForTest(s, true))
    { if (a.kind == Action::Kind::ActivatePump && a.gy_exile_mode == 6) { ++offered; CHECK(a.chosen_x == 1); } }
    CHECK(offered == 1);                                    // idempotent: K is 1

    // The shared apply grants haste; the creature can attack and the activation is no longer live.
    CHECK(ApplyActivatePump(s, 0, 1, 6, 1) == 1);
    CHECK(BNum(s, 1)->temp_haste);
    CHECK(CanAttackFull(*BNum(s, 1), s.battlefield, 0));
    CHECK_FALSE(SelfHasteActivationLive(s, 0, 1));
    CHECK_FALSE(OffersSelfHaste(s, 1, true));

    // A Flinthoof that has been here since last turn gains nothing: never offered.
    GameState t = BBase();
    BPut(t, "Mountain", 10);
    BPut(t, "Flinthoof Boar", 1, /*sick=*/false);
    CHECK_FALSE(SelfHasteActivationLive(t, 0, 1));
    CHECK_FALSE(OffersSelfHaste(t, 1, true));

    // ...nor one Lightning Greaves already hastes.
    GameState g = BBase();
    BPut(g, "Mountain", 10);
    BPut(g, "Flinthoof Boar", 1, /*sick=*/true);
    Permanent& greaves = BPut(g, "Lightning Greaves", 2);
    greaves.equipped_to = 1;
    CHECK_FALSE(SelfHasteActivationLive(g, 0, 1));
}

TEST_CASE("Flinthoof's haste cannot be paid with Somberwald Sage's creature-spell-only mana")
{
    GameState s = BBase();
    BPut(s, "Somberwald Sage", 20, /*sick=*/false);
    GameState t = s;
    {
        CreatureAbilityPayScope scope(&BNum(s, 20)->card);
        CHECK_FALSE(TapForCostDirect(t, BDef("Flinthoof Boar").params.self_haste_cost.value(), /*for_creature=*/false));
    }
    BPut(s, "Mountain", 10);
    GameState u = s;
    CHECK(TapForCostDirect(u, BDef("Flinthoof Boar").params.self_haste_cost.value(), /*for_creature=*/false));
}

TEST_CASE("Herald's Horn chooses Boar under BoarsProvider and discounts Boar creature spells by {1}")
{
    const CardDefinition& horn = BDef("Herald's Horn");
    CHECK(horn.params.chooses_creature_type);
    CHECK(horn.params.reduces_spell_subtype_amount == 1);
    CHECK(horn.params.reduces_spell_subtype_creature_only);
    CHECK(horn.params.upkeep_look_top_chosen_type_to_hand);

    BoarsProvider provider;
    GameState s = BBase();
    s.m_provider = &provider;
    // A Human-heavy library: the generic pick would be Human; BoarsProvider still names Boar (D4, PROVISIONAL).
    for (int i = 0; i < 6; ++i) { s.players[0].library.push_back(BCard("Somberwald Sage", 100 + i)); }
    s.players[0].library.push_back(BCard("Flinthoof Boar", 120));
    const uint16_t boar = SubtypeRegistry::Instance().Id("Boar");
    CHECK(DominantCreatureSubtypeId(s, 0) != boar);
    BPut(s, "Herald's Horn", 2);
    FireEtbWatchers(s, 0, static_cast<int>(s.battlefield.size()) - 1);
    CHECK(BNum(s, 2)->chosen_subtype_id == boar);
    // {1}{G} Flinthoof -> {G}; a non-Boar creature is not discounted.
    CHECK(EffectiveSpellCost(BDef("Flinthoof Boar"), s, 1).generic == 0);
    CHECK(EffectiveSpellCost(BDef("Somberwald Sage"), s, 1).generic == BDef("Somberwald Sage").card.m_mana_cost.generic);
}

TEST_CASE("creature_type decision: the human is offered every creature type the deck carries")
{
    BoarsProvider provider;
    GameState s = BBase();
    s.m_provider = &provider;
    s.players[0].library.push_back(BCard("Flinthoof Boar", 120));
    s.players[0].library.push_back(BCard("Flinthoof Boar", 121));
    s.players[0].library.push_back(BCard("Somberwald Sage", 122));
    std::vector<std::string> seen;
    int dflt = -2;
    CreatureTypeChooser choose = [&](const GameState&, int, const std::string& src,
                                     const std::vector<std::string>& menu, int heuristic_index) {
        CHECK(src == "Herald's Horn");
        seen = menu;
        dflt = heuristic_index;
        for (std::size_t i = 0; i < menu.size(); ++i) { if (menu[i] == "Human") { return static_cast<int>(i); } }
        return -1;
    };
    CreatureTypeChooser* prior = g_play_creature_type_chooser;
    g_play_creature_type_chooser = &choose;
    BPut(s, "Herald's Horn", 2);
    FireEtbWatchers(s, 0, static_cast<int>(s.battlefield.size()) - 1);
    g_play_creature_type_chooser = prior;
    REQUIRE(seen.size() == 3u);                           // Boar (2), then Human / Druid (1 each)
    CHECK(seen[0] == "Boar");
    CHECK(dflt == 0);                                     // the provider's pick is the default
    CHECK(BNum(s, 2)->chosen_subtype_id == SubtypeRegistry::Instance().Id("Human"));
    // RevealLogPause nulls the chooser (inert in every search / rollout scope).
    g_play_creature_type_chooser = &choose;
    {
        RevealLogPause pause;
        CHECK(g_play_creature_type_chooser == nullptr);
    }
    CHECK(g_play_creature_type_chooser == &choose);
    g_play_creature_type_chooser = prior;
}

namespace
{
GameState HornBoard(int hand_cards)
{
    GameState s = BBase();
    s.phase = Phase::PostCombatMain;
    Permanent& h = BPut(s, "Herald's Horn", 2);
    h.chosen_subtype_id = SubtypeRegistry::Instance().Id("Boar");
    BPut(s, "Forest", 10);
    for (int i = 0; i < hand_cards; ++i) { s.players[0].hand.push_back(BCard("Forest", 300 + i)); }
    s.players[0].library.push_back(BCard("Flinthoof Boar", 120));
    for (int i = 0; i < 6; ++i) { s.players[0].library.push_back(BCard("Forest", 200 + i)); }
    return s;
}
}   // namespace

TEST_CASE("Herald's Horn upkeep: a Boar on top is TAKEN when no cleanup discard is reachable (the fold)")
{
    GameState s = HornBoard(3);
    CHECK(HornTakeFoldHolds(s, 0, 0));
    const long long before = g_horn_unpinned.load();
    PerformUpkeepHornReveal(s);
    REQUIRE(s.players[0].hand.size() == 4u);
    CHECK(s.players[0].hand.back().m_name.str() == "Flinthoof Boar");
    CHECK(s.players[0].library.front().m_name.str() == "Forest");
    CHECK(g_horn_unpinned.load() == before);              // a fold, not an unpinned residual

    // A non-Boar on top stays there.
    GameState t = HornBoard(3);
    (void)t.players[0].library.DrawTop();
    PerformUpkeepHornReveal(t);
    CHECK(t.players[0].hand.size() == 3u);
}

TEST_CASE("Herald's Horn upkeep: a full hand makes take/leave a SEARCHED residual (pin bit = leave)")
{
    GameState s = HornBoard(6);                           // 6 + take + draw = 8 > 7: a discard is reachable
    CHECK_FALSE(HornTakeFoldHolds(s, 0, 0));
    GameState leave = s;
    leave.scripted_upkeep_horn = 1;
    PerformUpkeepHornReveal(leave);
    CHECK(leave.players[0].hand.size() == 6u);
    CHECK(leave.players[0].library.front().m_name.str() == "Flinthoof Boar");
    CHECK(leave.scripted_upkeep_horn == -1);              // consumed
    GameState take = s;
    take.scripted_upkeep_horn = 0;
    PerformUpkeepHornReveal(take);
    CHECK(take.players[0].hand.size() == 7u);
    // Unpinned (the playout default beyond the horizon): take, and the census counts it.
    GameState un = s;
    const long long before = g_horn_unpinned.load();
    PerformUpkeepHornReveal(un);
    CHECK(un.players[0].hand.size() == 7u);
    CHECK(g_horn_unpinned.load() == before + 1);
    // An UNAUDITED card where the fold looks (a draw spell in hand) also sends it to the search.
    GameState d = HornBoard(1);
    d.players[0].hand.push_back(BCard("Brainstorm", 400));
    CHECK_FALSE(HornTakeFoldHolds(d, 0, 0));
}

TEST_CASE("Herald's Horn upkeep: two Horns each look at the then-current top")
{
    GameState s = HornBoard(2);
    Permanent& h2 = BPut(s, "Herald's Horn", 3);
    h2.chosen_subtype_id = SubtypeRegistry::Instance().Id("Boar");
    s.players[0].library.insert(s.players[0].library.begin(), BCard("Flinthoof Boar", 121));
    PerformUpkeepHornReveal(s);
    CHECK(s.players[0].hand.size() == 4u);                // both Boars taken
    CHECK(s.players[0].library.front().m_name.str() == "Forest");
}

TEST_CASE("Herald's Horn upkeep: the human decides through the dig chooser (-1 leaves it on top)")
{
    GameState s = HornBoard(1);
    bool asked = false;
    DigChooser choose = [&](const GameState&, int, const std::string& src, const std::vector<Card>& examined,
                            const std::vector<int>&, int) {
        asked = true;
        CHECK(src == "Herald's Horn (upkeep reveal)");
        CHECK(examined.size() == 1u);
        return -1;
    };
    DigChooser* prior = g_play_dig_chooser;
    g_play_dig_chooser = &choose;
    PerformUpkeepHornReveal(s);
    g_play_dig_chooser = prior;
    CHECK(asked);
    CHECK(s.players[0].hand.size() == 1u);
    CHECK(s.players[0].library.front().m_name.str() == "Flinthoof Boar");
}

TEST_CASE("Herald's Horn: the BoundaryFan crosses take and leave for a residual decision and records the mask")
{
    GameState s = HornBoard(6);
    const auto crossings = TurnSolver::HornBoundaryForTest(s);
    REQUIRE(crossings.size() == 2u);
    CHECK(crossings[0].first == 0);                       // take first (the playout default keeps every tie)
    CHECK(crossings[1].first == 1);                       // leave
    CHECK(crossings[0].second == crossings[1].second + 1);
    // A small hand folds: one crossing, nothing recorded.
    GameState f = HornBoard(2);
    const auto folded = TurnSolver::HornBoundaryForTest(f);
    REQUIRE(folded.size() == 1u);
    CHECK(folded[0].first == -2);
}

TEST_CASE("Boars routes to BoarsProvider (above the fetchlands' anti signature)")
{
    BDef("Flinthoof Boar");
    const DecisionProvider& p = SelectDecisionProvider(DeckLoader::LoadFromFile(
        ResolveHeuristicDefaultsPath("decks/Boars/Boars.cod")));
    CHECK(std::string(p.Name()) == "Boars");
}
