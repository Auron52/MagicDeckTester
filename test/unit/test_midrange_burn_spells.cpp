#include <doctest/doctest.h>
#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

extern thread_local std::vector<std::string>* g_line_order_capture;

namespace {
const CardDefinition& MBDef(const std::string& name)
{
    static const bool loaded = [] { CardDatabase::Instance().LoadFromJson(
        ResolveHeuristicDefaultsPath("src/cards/data/cards.json")); return true; }();
    REQUIRE(loaded);
    const auto* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    return *d;
}
Card MBCard(const std::string& name, int id)
{ Card c = MBDef(name).card; c.m_number = id; return c; }
void MBPut(GameState& s, const std::string& name, int id, int who = 0, bool tapped = false)
{
    Permanent p; p.card = MBCard(name, id); p.controller_index = who; p.owner_index = who;
    p.tapped = tapped; p.entered_this_turn = false; s.battlefield.push_back(p);
}
struct MBHooks
{
    FreeCastChooser* fc = g_play_free_cast_chooser;
    TopChooser* top = g_play_top_chooser;
    TargetChooser* target = g_play_target_chooser;
    BounceChooser* sacrifice = g_play_sacrifice_chooser;
    ~MBHooks() { g_play_free_cast_chooser = fc; g_play_top_chooser = top;
                 g_play_target_chooser = target; g_play_sacrifice_chooser = sacrifice; }
};
}

TEST_CASE("midrange burn: autonomous burn keeps enemy targets and viewer keeps all legal targets")
{
    GameState s; s.active_player_index = 0;
    static const MidrangeBurnProvider provider; s.m_provider = &provider;
    MBPut(s, "Monastery Swiftspear", 101, 0);
    MBPut(s, "Monastery Swiftspear", 102, 1);
    s.players[0].hand.push_back(MBCard("Grim Lavamancer", 103));
    const auto bolt = FixedBurnCandidates(s, 0, MBDef("Lightning Bolt"), true);
    bool face = false, own_life = false, own_board = false, own_hand = false, enemy = false;
    for (const FixedBurnPin& pin : bolt)
    {
        face |= pin.first == 0;
        own_life |= pin.first == -1;
        own_board |= pin.first == 101;
        own_hand |= pin.first == 103;
        enemy |= pin.first == 102;
    }
    CHECK(face);
    CHECK(enemy);
    const bool complete = HumanPlayActive() || DecisionUnpruned(UnprunedGate::FixedBurnTarget)
        || !provider.PrunesOwnFixedBurnTargets();
    CHECK(own_life == complete);
    CHECK(own_board == complete);
    CHECK(own_hand == complete);
    const auto blaze = FixedBurnCandidates(s, 0, MBDef("Searing Blaze"), true);
    bool own_blaze = false, enemy_blaze = false;
    for (const FixedBurnPin& pin : blaze)
    {
        own_blaze |= pin.first == -1;
        enemy_blaze |= pin.first == 0 && pin.second == 102;
    }
    CHECK(enemy_blaze);
    CHECK(own_blaze);
}

TEST_CASE("midrange burn: autonomous burn folds inert spawn targets but keeps the human menu")
{
    GameState s; s.active_player_index = 0;
    static const MidrangeBurnProvider provider; s.m_provider = &provider;
    static const std::vector<OpponentSpawn> spawns = {{1, 1, 1}, {1, 6, 6}};
    s.opponent_spawns = &spawns;
    for (int i = 0; i < 2; ++i)
    {
        Permanent p;
        p.card.m_name = i == 0 ? "1/1 Creature" : "6/6 Creature";
        p.card.RehashName(); p.card.AddType(CardType::Creature);
        p.card.m_toughness = i == 0 ? 1 : 6;
        p.card.m_number = 500000 + i;
        p.controller_index = p.owner_index = 1;
        s.battlefield.push_back(p);
    }
    const auto bolt = FixedBurnCandidates(s, 0, MBDef("Lightning Bolt"));
    const auto blaze = FixedBurnCandidates(s, 0, MBDef("Searing Blaze"));
    const bool complete = HumanPlayActive() || DecisionUnpruned(UnprunedGate::FixedBurnTarget)
        || !provider.PrunesPassiveBurnTargets();
    CHECK(std::any_of(bolt.begin(), bolt.end(), [](const FixedBurnPin& p) { return p.first == 0; }));
    CHECK(std::any_of(bolt.begin(), bolt.end(), [](const FixedBurnPin& p) { return p.first == 500000; }) == complete);
    CHECK(std::any_of(bolt.begin(), bolt.end(), [](const FixedBurnPin& p) { return p.first == 500001; }) == complete);
    CHECK(std::any_of(blaze.begin(), blaze.end(), [](const FixedBurnPin& p) { return p.second == 500001; }));
    CHECK(std::any_of(blaze.begin(), blaze.end(), [](const FixedBurnPin& p) { return p.second == 500000; }) == complete);
    MBPut(s, "Monastery Swiftspear", 10);
    const auto with_own = FixedBurnCandidates(s, 0, MBDef("Searing Blaze"));
    CHECK(std::any_of(with_own.begin(), with_own.end(), [](const FixedBurnPin& p) { return p.first == -1; }) == complete);
}

TEST_CASE("midrange burn: cleanup sheds dead burn before optional mana and protects the core")
{
    GameState s; s.active_player_index = 0;
    static const MidrangeBurnProvider provider; s.m_provider = &provider;
    const std::vector<std::string> names = {"Price of Progress", "Searing Blaze", "Mountain",
        "Mountain", "Mountain", "Monastery Swiftspear", "Lightning Bolt", "Chain Lightning",
        "Magma Jet", "Chandra, Torch of Defiance"};
    for (int i = 0; i < static_cast<int>(names.size()); ++i)
    { s.players[0].hand.push_back(MBCard(names[static_cast<std::size_t>(i)], 200 + i)); }
    for (int i = 0; i < 3; ++i)
    {
        Permanent p;
        p.card.m_name = "Opponent Basic Land";
        p.card.RehashName();
        p.card.AddType(CardType::Land);
        p.card.AddSupertype(Supertype::Basic);
        p.controller_index = p.owner_index = 1;
        s.battlefield.push_back(p);
    }
    const auto rank = provider.CleanupDiscardShedOrder(s, nullptr);
    const bool enabled = provider.CleanupDiscardShedStable() == false;
    if (!enabled)
    {
        CHECK(rank.size() == names.size());
        CHECK(provider.CleanupDiscardCandidates(s, nullptr).size() == names.size());
        return;
    }
    REQUIRE(rank.size() == names.size());
    CHECK(rank[0] == 0); // no opponent nonbasics
    CHECK(rank[1] == 1); // no opposing creature
    const auto pos = [&](int i)
    { return std::find(rank.begin(), rank.end(), i) - rank.begin(); };
    CHECK(pos(4) < pos(8)); // third land yields to damage still needed near lethal
    CHECK(pos(4) < pos(6)); // one-drop burn is a higher priority
    CHECK(pos(4) < pos(5)); // one-drop creature is a higher priority
    CHECK(pos(2) > pos(4));
    CHECK(pos(3) > pos(4));
    CHECK(provider.CleanupDiscardCandidates(s, nullptr) == std::vector<int>{0});
    GameState nonbasic = s;
    Card other_land;
    other_land.m_name = "Opponent Nonbasic Land";
    other_land.RehashName();
    other_land.AddType(CardType::Land);
    nonbasic.battlefield[0].card = other_land;
    CHECK(provider.CleanupDiscardShedOrder(nonbasic, nullptr).front() == 1);
}

TEST_CASE("midrange burn: cleanup retains Mountain fuel for chosen Fireblast")
{
    GameState s; s.active_player_index = 0;
    static const MidrangeBurnProvider provider; s.m_provider = &provider;
    MBPut(s, "Mountain", 300);
    MBPut(s, "Fiery Islet", 301);
    const std::vector<std::string> names = {"Fiery Islet", "Mountain", "Fireblast",
        "Lightning Bolt", "Chain Lightning", "Magma Jet", "Chandra, Torch of Defiance", "Mountain"};
    for (int i = 0; i < static_cast<int>(names.size()); ++i)
    { s.players[0].hand.push_back(MBCard(names[static_cast<std::size_t>(i)], 310 + i)); }
    if (provider.CleanupDiscardShedStable()) { return; } // flag-off generic control
    const auto rank = provider.CleanupDiscardShedOrder(s, nullptr);
    const auto pos = [&](int i)
    { return std::find(rank.begin(), rank.end(), i) - rank.begin(); };
    CHECK(pos(1) > pos(0)); // Mountain pays Fireblast; Islet cannot
    CHECK(pos(2) > pos(0));
    CHECK(pos(7) < pos(1)); // fourth total land is optional
}

TEST_CASE("midrange burn: Light Up fills a damage shortage before optional lands")
{
    GameState s; s.active_player_index = 0; s.phase = Phase::Ending;
    static const MidrangeBurnProvider provider; s.m_provider = &provider;
    MBPut(s, "Mountain", 400); MBPut(s, "Mountain", 401);
    const std::vector<std::string> names = {"Light Up the Stage", "Mountain", "Mountain",
        "Monastery Swiftspear", "Lightning Bolt", "Chain Lightning", "Magma Jet", "Chandra, Torch of Defiance"};
    for (int i = 0; i < static_cast<int>(names.size()); ++i)
    { s.players[0].hand.push_back(MBCard(names[static_cast<std::size_t>(i)], 410 + i)); }
    if (provider.CleanupDiscardShedStable()) { return; } // flag-off generic control
    const auto rank = provider.CleanupDiscardShedOrder(s, nullptr);
    const auto pos = [&](int i)
    { return std::find(rank.begin(), rank.end(), i) - rank.begin(); };
    CHECK(pos(0) > pos(1));
    CHECK(pos(0) > pos(2));
}

TEST_CASE("midrange burn: cleanup landfall reach does not depend on hand order")
{
    static const MidrangeBurnProvider provider;
    const std::vector<std::string> names = {"Searing Blaze", "Mountain", "Lightning Bolt",
        "Magma Jet", "Fireblast", "Monastery Swiftspear", "Fiery Islet", "Chandra, Torch of Defiance"};
    auto rank_names = [&](bool move_land_first)
    {
        GameState s; s.active_player_index = 0; s.phase = Phase::Ending;
        s.players[1].life = 14; s.m_provider = &provider;
        MBPut(s, "Mountain", 500); MBPut(s, "Mountain", 501);
        MBPut(s, "Monastery Swiftspear", 502, 1);
        for (int i = 0; i < static_cast<int>(names.size()); ++i)
        { s.players[0].hand.push_back(MBCard(names[static_cast<std::size_t>(i)], 510 + i)); }
        if (move_land_first) { std::swap(s.players[0].hand[0], s.players[0].hand[1]); }
        std::vector<std::string> out;
        for (int i : provider.CleanupDiscardShedOrder(s, nullptr))
        { out.push_back(s.players[0].hand[static_cast<std::size_t>(i)].m_name.str()); }
        return out;
    };
    if (provider.CleanupDiscardShedStable()) { return; } // flag-off generic control
    CHECK(rank_names(false) == rank_names(true));
}

TEST_CASE("midrange burn: a fresh Chandra can survive the legend rule and human play chooses")
{
    MBHooks restore;
    GameState s; s.active_player_index = 0;
    static const MidrangeBurnProvider provider; s.m_provider = &provider;
    MBPut(s, "Chandra, Torch of Defiance", 101);
    MBPut(s, "Chandra, Torch of Defiance", 102);
    s.battlefield[0].loyalty = 5;
    s.battlefield[0].loyalty_activated_this_turn = true;
    s.battlefield[1].loyalty = 4;
    CHECK(provider.LegendKeepIndex(s, 0, {0, 1}) == 1);
    s.battlefield[0].loyalty = 7;
    CHECK(provider.LegendKeepIndex(s, 0, {0, 1}) == 0);
    s.battlefield[0].loyalty = 5;
    int prompts = 0;
    BounceChooser choose_old = [&](const GameState&, int, const std::string& source,
                                   const std::vector<int>& legal, int recommended)
    {
        CHECK(source == "Legend rule: choose Chandra to keep");
        const std::vector<int> expected{0, 1};
        CHECK(legal == expected);
        CHECK(recommended == 1);
        ++prompts;
        return 0;
    };
    g_play_sacrifice_chooser = &choose_old;
    EnforceLegendRule(s, 0);
    CHECK(prompts == 1);
    REQUIRE(s.battlefield.size() == 1);
    CHECK(s.battlefield[0].card.m_number == 101);
    REQUIRE(s.players[0].graveyard.size() == 1);
    CHECK(s.players[0].graveyard[0].m_number == 102);
}

TEST_CASE("midrange burn: Price counts each player's current nonbasic lands independently")
{
    GameState s; s.active_player_index = 0;
    MBPut(s, "Mountain", 1); MBPut(s, "Snow-Covered Mountain", 2);
    MBPut(s, "Steam Vents", 3); MBPut(s, "Steam Vents", 4, 1);
    MBPut(s, "Karplusan Forest", 5, 1);
    CHECK(NonbasicLandDamage(s, 0, 2) == 2);
    CHECK(NonbasicLandDamage(s, 1, 2) == 4);
    ResolveNonbasicLandDamage(s, 0, 2);
    CHECK(s.players[0].life == 18); CHECK(s.players[1].life == 16);
    CHECK(s.opponent_lost_life_this_turn);
    GameState empty; ResolveNonbasicLandDamage(empty, 0, 2);
    CHECK(empty.players[0].life == 20); CHECK(empty.players[1].life == 20);
}

TEST_CASE("midrange burn: Fireblast alternative accepts tapped and nonbasic Mountains atomically")
{
    GameState s; s.active_player_index = 0;
    MBPut(s, "Mountain", 101, 0, true); MBPut(s, "Steam Vents", 102, 0, true);
    MBPut(s, "Karplusan Forest", 103);
    CHECK(FireblastMountains(s, 0).size() == 2);
    CHECK_FALSE(PayFireblastAlternative(s, 0, "fireblast:101,103"));
    CHECK(s.battlefield.size() == 3); CHECK(s.players[0].graveyard.empty());
    REQUIRE(PayFireblastAlternative(s, 0, "fireblast:101,102"));
    REQUIRE(s.battlefield.size() == 1); CHECK(s.battlefield[0].card.m_name == "Karplusan Forest");
    CHECK(s.players[0].graveyard.size() == 2);
    CHECK(MBDef("Fireblast").card.m_mana_cost.ManaValue() == 6);
}

TEST_CASE("midrange burn: human Fireblast floats mana from chosen Mountains before sacrifice")
{
    MBHooks restore;
    GameState s; s.active_player_index = 0; s.turn_number = 5;
    for (int i = 0; i < 5; ++i) { MBPut(s, "Mountain", 101 + i); }
    BounceChooser choose = [](const GameState&, int, const std::string&,
                              const std::vector<int>&, int recommended) { return recommended; };
    g_play_sacrifice_chooser = &choose;
    REQUIRE(PayFireblastAlternative(s, 0, "fireblast:101,102"));
    CHECK(s.floating_mana.red == 2);
    REQUIRE(s.battlefield.size() == 3);
    CHECK(std::all_of(s.battlefield.begin(), s.battlefield.end(),
                      [](const Permanent& p) { return !p.tapped; }));
    CHECK(s.players[0].graveyard.size() == 2);
}

TEST_CASE("midrange burn: a Fireblast alternative plan pays zero mana and sacrifices its pair")
{
    GameState s; s.active_player_index = 0; s.turn_number = 3;
    MBPut(s, "Mountain", 101, 0, true); MBPut(s, "Mountain", 102, 0, true);
    s.players[0].hand.push_back(MBCard("Fireblast", 200));
    TurnSolver::Plan plan; Action a; a.kind = Action::Kind::CastFromHand;
    a.card_name = "Fireblast"; a.hand_index = 0; a.tutor_target = "fireblast:101,102";
    plan.actions.push_back(a);
    TurnSolver::ApplyPlan(s, plan, true);
    CHECK(s.players[1].life == 16); CHECK(s.battlefield.empty()); CHECK(s.players[0].hand.empty());
    CHECK(s.free_casts_available == 0);
}

TEST_CASE("midrange burn: Fireblast cost choices keep paid mode and prefer tapped Mountains")
{
    GameState s; s.active_player_index = 0; s.turn_number = 6;
    static const MidrangeBurnProvider provider; s.m_provider = &provider;
    for (int id = 101; id <= 108; ++id) { MBPut(s, "Mountain", id, 0, id <= 102); }
    s.players[0].hand.push_back(MBCard("Fireblast", 200));
    CHECK(FireblastMountains(s, 0).size() == 8);
    CHECK(provider.OffersPaidFireblastCast(s));
    const auto actions = TurnSolver::CollectActionsForTest(s, true);
    int paid = 0, alternative = 0;
    bool chose_tapped = false;
    for (const Action& a : actions)
    {
        if (a.card_name != "Fireblast") { continue; }
        if (a.tutor_target.str().rfind("fireblast:", 0) != 0) { ++paid; CHECK(a.cost.ManaValue() == 6); continue; }
        ++alternative;
        chose_tapped |= a.tutor_target.str().rfind("fireblast:101,102", 0) == 0;
    }
    const int targets = HumanPlayActive() ? 2 : 1; // enemy face and own face in the viewer
    CHECK(paid == targets);
    const bool full = HumanPlayActive() || DecisionUnpruned(UnprunedGate::FireblastPair)
        || !EnvOn("MTG_MIDRANGE_FIREBLAST_PAIR", true);
    CHECK(alternative == (full ? 28 : 1) * targets);
    CHECK(chose_tapped);
}

TEST_CASE("midrange burn: unreachable paid Fireblast cost is omitted from autonomous actions")
{
    GameState s; s.active_player_index = 0; s.turn_number = 3;
    static const MidrangeBurnProvider provider; s.m_provider = &provider;
    MBPut(s, "Mountain", 101); MBPut(s, "Mountain", 102);
    s.players[0].hand.push_back(MBCard("Fireblast", 200));
    const auto actions = TurnSolver::CollectActionsForTest(s, true);
    int paid = 0, alternative = 0;
    for (const Action& a : actions)
    {
        if (a.card_name != "Fireblast") { continue; }
        const bool is_alternative = a.tutor_target.str().rfind("fireblast:", 0) == 0;
        paid += !is_alternative;
        alternative += is_alternative;
    }
    const bool full = HumanPlayActive() || DecisionUnpruned(UnprunedGate::FireblastPaid)
        || !EnvOn("MTG_MIDRANGE_FIREBLAST_PAID", true);
    const int targets = HumanPlayActive() ? 2 : 1;
    CHECK(paid == (full ? targets : 0));
    CHECK(alternative == targets);
}

TEST_CASE("midrange burn: Fireblast paid-cost bound counts floating mana once")
{
    GameState s; s.active_player_index = 0; s.turn_number = 5;
    static const MidrangeBurnProvider provider; s.m_provider = &provider;
    for (int id = 101; id <= 104; ++id) { MBPut(s, "Mountain", id); }
    s.floating_mana.red = 1;
    CHECK(AvailableManaPool(s, nullptr).Total() == 5);
    CHECK(provider.OffersPaidFireblastCast(s)
          == !EnvOn("MTG_MIDRANGE_FIREBLAST_PAID", true));
}

TEST_CASE("midrange burn: Grim Lavamancer fuel choice keeps every damage target")
{
    GameState s; s.active_player_index = 0; s.turn_number = 4;
    static const MidrangeBurnProvider provider; s.m_provider = &provider;
    MBPut(s, "Grim Lavamancer", 101); MBPut(s, "Mountain", 102);
    s.players[0].graveyard = {MBCard("Mountain", 201), MBCard("Lightning Bolt", 202),
                              MBCard("Fireblast", 203)};
    const auto actions = TurnSolver::CollectActionsForTest(s, true);
    int fuel_face = 0, fuel_self = 0, fuel_creature = 0;
    bool chose_canonical = false;
    for (const Action& a : actions)
    {
        if (a.kind != Action::Kind::GraveyardExileAbility || a.gy_exile_mode != 3) { continue; }
        fuel_face += a.enchant_target == 0;
        fuel_self += a.enchant_target == kAbilitySelfTarget;
        fuel_creature += a.enchant_target == 101;
        chose_canonical |= a.tutor_target == GraveyardDamageFuelSpec("Fireblast", "Lightning Bolt");
    }
    const bool full = HumanPlayActive() || DecisionUnpruned(UnprunedGate::LavamancerFuel)
        || !EnvOn("MTG_MIDRANGE_LAVAMANCER_FUEL", true);
    const int expected = full ? 3 : 1;
    CHECK(fuel_face == expected);
    CHECK(fuel_self == expected);
    CHECK(fuel_creature == expected);
    CHECK(chose_canonical);
}

TEST_CASE("midrange burn: Rift Bolt arrival deals damage, triggers Eidolon and prowess before draw")
{
    GameState s; s.active_player_index = 0; s.turn_number = 2;
    MBPut(s, "Eidolon of the Great Revel", 1); MBPut(s, "Monastery Swiftspear", 2);
    s.players[0].suspended_cards.push_back({MBCard("Rift Bolt", 3), 2});
    ProcessSuspendArrivals(s, 0);
    CHECK(s.players[0].life == 18); CHECK(s.players[1].life == 17);
    CHECK(s.opponent_lost_life_this_turn); CHECK(s.spells_cast_this_turn == 1);
    CHECK(s.battlefield[1].temp_power_bonus == 1);
    CHECK(s.players[0].graveyard.size() == 1); CHECK(s.players[0].suspended_cards.empty());
}

TEST_CASE("midrange burn: decline Rift Bolt leaves it permanently exiled without a later reprompt")
{
    MBHooks restore;
    GameState s; s.active_player_index = 0; s.turn_number = 2;
    int prompts = 0;
    FreeCastChooser decline = [&](const GameState&, int, const std::string&, const std::vector<Card>&,
                                  int, const std::vector<std::string>&) { ++prompts; return -1; };
    g_play_free_cast_chooser = &decline;
    s.players[0].suspended_cards.push_back({MBCard("Rift Bolt", 3), 2});
    ProcessSuspendArrivals(s, 0);
    REQUIRE(s.players[0].suspended_cards.size() == 1);
    CHECK(s.players[0].suspended_cards[0].arrive_turn == -1); CHECK(s.spells_cast_this_turn == 0);
    s.turn_number = 3; ProcessSuspendArrivals(s, 0);
    CHECK(prompts == 1); CHECK(s.players[1].life == 20);
}

TEST_CASE("midrange burn: provider does not force a lethal Eidolon Rift Bolt cast")
{
    GameState s; s.active_player_index = 0; s.turn_number = 2; s.players[0].life = 2;
    MBPut(s, "Eidolon of the Great Revel", 1);
    s.players[0].suspended_cards.push_back({MBCard("Rift Bolt", 3), 2});
    ProcessSuspendArrivals(s, 0);
    CHECK(s.players[0].life == 2); CHECK(s.players[1].life == 20);
    CHECK(s.spells_cast_this_turn == 0); CHECK(s.players[0].suspended_cards.size() == 1);
}

TEST_CASE("midrange burn: Magma Jet deals damage before offering scry and preserves chosen order")
{
    MBHooks restore;
    GameState s; s.active_player_index = 0;
    s.players[0].library = {};

    for (const Card& c : {MBCard("Mountain", 1), MBCard("Lightning Bolt", 2)}) { s.players[0].library.push_back(c); }
    bool offered = false;
    TopChooser top = [&](const GameState& ss, const std::string&, const std::vector<Card>& cards, LookKind kind)
    { offered = true; CHECK(ss.players[1].life == 18); CHECK(kind == LookKind::Scry);
      CHECK(cards.size() == 2); TopDisposition d; d.top_order = {1, 0}; return d; };
    g_play_top_chooser = &top;
    StackEntry entry; entry.type = StackEntry::EntryType::Spell; entry.source = MBCard("Magma Jet", 9); entry.controller_index = 0;
    Target t; t.type = Target::Type::Player; t.player_index = 1; entry.targets.push_back(t);
    REQUIRE(EffectHandler::Resolve(s, entry, MBDef("Magma Jet")));
    CHECK(offered); CHECK(s.players[0].library[0].m_name == "Lightning Bolt");
    CHECK(s.players[0].graveyard.size() == 1);
}

TEST_CASE("midrange burn: Chain Lightning copy pays RR and is not a cast")
{
    MBHooks restore;
    GameState s; s.active_player_index = 0;
    MBPut(s, "Mountain", 1); MBPut(s, "Mountain", 2);
    MBPut(s, "Eidolon of the Great Revel", 3); MBPut(s, "Monastery Swiftspear", 4);
    int prompts = 0;
    FreeCastChooser accept = [&](const GameState&, int, const std::string&, const std::vector<Card>&,
                                 int, const std::vector<std::string>&) { ++prompts; return 0; };
    g_play_free_cast_chooser = &accept;
    ContinueChainLightning(s, MBDef("Chain Lightning"), 0);
    CHECK(prompts == 2); CHECK(s.players[1].life == 17); CHECK(s.players[0].life == 20);
    CHECK(s.battlefield[0].tapped); CHECK(s.battlefield[1].tapped);
    CHECK(s.spells_cast_this_turn == 0); CHECK(s.battlefield[3].temp_power_bonus == 0);
}


TEST_CASE("midrange burn: real-stack free Bolt retains its target and deals three")
{
    GameState s; s.active_player_index = 0;
    REQUIRE(EffectHandler::PushFreeCast(s, MBCard("Lightning Bolt", 901), 0));
    REQUIRE(s.stack.size() == 1);
    StackEntry entry = s.stack.back(); s.stack.pop_back();
    FixedBurnPin pin;
    REQUIRE(DecodeFixedBurnPin(entry.tutor_target, pin));
    CHECK(pin.first == 0);
    REQUIRE(EffectHandler::Resolve(s, entry, MBDef("Lightning Bolt")));
    CHECK(s.players[1].life == 17);
    CHECK(s.spells_cast_this_turn == 1);
}

TEST_CASE("midrange burn: real-stack free Blaze casts only with a legal paired target")
{
    GameState empty; empty.active_player_index = 0;
    CHECK_FALSE(EffectHandler::PushFreeCast(empty, MBCard("Searing Blaze", 902), 0));
    CHECK(empty.stack.empty()); CHECK(empty.spells_cast_this_turn == 0);

    GameState s; s.active_player_index = 0;
    MBPut(s, "Monastery Swiftspear", 903, 1);
    REQUIRE(EffectHandler::PushFreeCast(s, MBCard("Searing Blaze", 902), 0));
    REQUIRE(s.stack.size() == 1);
    StackEntry entry = s.stack.back(); s.stack.pop_back();
    FixedBurnPin pin;
    REQUIRE(DecodeFixedBurnPin(entry.tutor_target, pin));
    CHECK(pin.first == 0); CHECK(pin.second == 903); CHECK(pin.paired_controller == 1);
    REQUIRE(EffectHandler::Resolve(s, entry, MBDef("Searing Blaze")));
    CHECK(s.players[1].life == 19);
    REQUIRE(s.battlefield.size() == 1);
    CHECK(s.battlefield.front().damage == 1);
}


TEST_CASE("midrange burn: suspend skips equal-named staged exile before the real hand copy")
{
    GameState s; s.active_player_index = 0; s.turn_number = 3;
    MBPut(s, "Mountain", 920);
    Card staged = MBCard("Rift Bolt", 921);
    staged.m_is_staged = true; staged.m_staged_expiry = 4;
    s.players[0].hand.push_back(staged);
    s.players[0].hand.push_back(MBCard("Rift Bolt", 922));
    ApplySuspend(s, 0, "Rift Bolt");
    REQUIRE(s.players[0].suspended_cards.size() == 1);
    CHECK(s.players[0].suspended_cards.front().card.m_number == 922);
    CHECK(s.players[0].suspended_cards.front().arrive_turn == 4);
    REQUIRE(s.players[0].hand.size() == 1);
    CHECK(s.players[0].hand.front().m_number == 921);
    CHECK(s.players[0].hand.front().m_is_staged);

    GameState exile_only; exile_only.active_player_index = 0; exile_only.turn_number = 3;
    MBPut(exile_only, "Mountain", 923);
    exile_only.players[0].hand.push_back(staged);
    ApplySuspend(exile_only, 0, "Rift Bolt");
    CHECK(exile_only.players[0].suspended_cards.empty());
    CHECK_FALSE(exile_only.battlefield.front().tapped);
}

TEST_CASE("midrange burn: all-illegal Magma Jet does not resolve scry")
{
    MBHooks restore;
    GameState s; s.active_player_index = 0;
    s.players[0].library = {};

    for (const Card& c : {MBCard("Mountain", 931), MBCard("Lightning Bolt", 932)}) { s.players[0].library.push_back(c); }
    bool offered = false;
    TopChooser top = [&](const GameState&, const std::string&, const std::vector<Card>&, LookKind)
    { offered = true; return TopDisposition{}; };
    g_play_top_chooser = &top;
    StackEntry entry; entry.type = StackEntry::EntryType::Spell;
    entry.source = MBCard("Magma Jet", 933); entry.controller_index = 0;
    entry.tutor_target = EncodeFixedBurnPin({934, -2, -1}); // legal when cast, now left battlefield
    CHECK_FALSE(ResolveFixedBurnPin(s, 0, MBDef("Magma Jet"), {934, -2, -1}, 2));
    REQUIRE(EffectHandler::Resolve(s, entry, MBDef("Magma Jet")));
    CHECK_FALSE(offered);
    CHECK(s.players[1].life == 20);
    REQUIRE(s.players[0].library.size() == 2);
    CHECK(s.players[0].library[0].m_number == 931);
    REQUIRE(s.players[0].graveyard.size() == 1);
    CHECK(s.players[0].graveyard[0].m_number == 933);
}

TEST_CASE("midrange burn: Blaze still resolves when only its player target remains")
{
    GameState s; s.active_player_index = 0;
    CHECK(ResolveFixedBurnPin(s, 0, MBDef("Searing Blaze"), {0, 941, 1}, 3));
    CHECK(s.players[1].life == 17);
    GameState none; none.active_player_index = 0;
    CHECK_FALSE(ResolveFixedBurnPin(none, 0, MBDef("Searing Blaze"), {942, 941, 1}, 3));
    CHECK(none.players[1].life == 20);
}


TEST_CASE("midrange burn: suspended Rift Bolt chooses its target before cast triggers and prowess")
{
    MBHooks restore;
    GameState s; s.active_player_index = 0; s.turn_number = 2;
    MBPut(s, "Monastery Swiftspear", 951);
    s.players[0].suspended_cards.push_back({MBCard("Rift Bolt", 952), 2});
    int prompts = 0;
    TargetChooser chooser = [&](const GameState& at_choice, const CardDefinition&, int, int, int,
                                const std::vector<ChosenTarget>&) -> std::vector<ChosenTarget>
    {
        ++prompts;
        CHECK(at_choice.spells_cast_this_turn == 0);
        REQUIRE(at_choice.battlefield.size() == 1);
        CHECK(at_choice.battlefield.front().temp_power_bonus == 0);
        CHECK(at_choice.battlefield.front().temp_tough_bonus == 0);
        return {{0, 1, 0}};
    };
    g_play_target_chooser = &chooser;
    ProcessSuspendArrivals(s, 0);
    CHECK(prompts == 1);
    CHECK(s.spells_cast_this_turn == 1);
    CHECK(s.players[1].life == 17);
    CHECK(s.battlefield.front().temp_power_bonus == 1);
}

TEST_CASE("midrange burn: cast restrictions strand a suspend arrival without casting it")
{
    GameState s; s.active_player_index = 0; s.turn_number = 2;
    s.casts_remaining_this_turn = 0;
    s.players[0].suspended_cards.push_back({MBCard("Rift Bolt", 961), 2});
    ProcessSuspendArrivals(s, 0);
    CHECK(s.spells_cast_this_turn == 0); CHECK(s.players[1].life == 20);
    REQUIRE(s.players[0].suspended_cards.size() == 1);
    CHECK(s.players[0].suspended_cards.front().arrive_turn == -1);
    CHECK(s.players[0].graveyard.empty());
}

TEST_CASE("midrange burn: Incinerator discount counts opponent noncombat damage from any source")
{
    GameState s; s.active_player_index = 0; s.deck_tracks_noncombat_damage = true;
    const auto& inc = MBDef("Chandra's Incinerator");
    CHECK(EffectiveSpellCost(inc, s, 1).ManaValue() == 6);
    RecordNoncombatDamageToPlayer(s, 0, 1, 3);
    CHECK(EffectiveSpellCost(inc, s, 1).ManaValue() == 3);
    RecordNoncombatDamageToPlayer(s, 1, 1, 3); // opponent hurts themselves: still counts
    CHECK(EffectiveSpellCost(inc, s, 1).ManaValue() == 1);
    RecordNoncombatDamageToPlayer(s, 0, 0, 5); // our own damage never reduces our Incinerator
    CHECK(EffectiveSpellCost(inc, s, 1).ManaValue() == 1);
    CHECK(inc.card.m_mana_cost.ManaValue() == 6);
}

TEST_CASE("midrange burn: two Bolts make Incinerator payable for R in the same plan")
{
    GameState s; s.active_player_index = 0; s.turn_number = 3; s.deck_tracks_noncombat_damage = true;
    static const MidrangeBurnProvider provider; s.m_provider = &provider;
    MBPut(s, "Mountain", 10); MBPut(s, "Mountain", 11); MBPut(s, "Mountain", 12);
    s.players[0].hand = {MBCard("Lightning Bolt", 20), MBCard("Lightning Bolt", 21),
                          MBCard("Chandra's Incinerator", 22)};
    TurnSolver::Plan p;
    for (int i = 0; i < 3; ++i)
    {
        Action a; a.kind = Action::Kind::CastFromHand; a.hand_index = i;
        a.card_name = s.players[0].hand[i].m_name;
        a.def = &MBDef(a.card_name.str());
        a.cost = EffectiveSpellCost(*a.def, s, 1);
        a.card_mv = a.def->card.m_mana_cost.ManaValue();
        a.is_noncreature = !a.def->card.IsCreature();
        a.direct_damage = a.def->params.damage;
        p.actions.push_back(a);
    }
    std::vector<std::string> executed_order;
    g_line_order_capture = &executed_order;
    TurnSolver::ApplyPlan(s, p, true);
    g_line_order_capture = nullptr;
    std::string order_trace;
    for (const std::string& step : executed_order) { order_trace += step + "; "; }
    INFO("order=" << order_trace);
    CHECK(s.players[1].life == 14); CHECK(s.noncombat_damage_received_this_turn[1] == 6);
    GameState payment_probe = s;
    const ManaCost inc_now = EffectiveSpellCost(MBDef("Chandra's Incinerator"), s, 1);
    const bool direct_can_pay = TapForCostDirect(payment_probe, inc_now, true);
    INFO("hand=" << s.players[0].hand.size()
         << " mana=" << AvailableManaPool(s, nullptr).Total()
         << " inc_cost=" << inc_now.ManaValue() << " r=" << inc_now.red
         << " canpay=" << direct_can_pay
         << " casts=" << s.spells_cast_this_turn << " life=" << s.players[0].life);
    CHECK(s.players[0].hand.empty());
    CHECK(std::any_of(s.battlefield.begin(), s.battlefield.end(), [](const Permanent& q)
        { return q.card.m_name == "Chandra's Incinerator"; }));
}

TEST_CASE("midrange burn: a second Bolt and reduced Incinerator are enumerated with two Mountains")
{
    GameState s; s.active_player_index = 0; s.turn_number = 5;
    s.deck_tracks_noncombat_damage = true;
    static const MidrangeBurnProvider provider; s.m_provider = &provider;
    MBPut(s, "Mountain", 10); MBPut(s, "Mountain", 11);
    RecordNoncombatDamageToPlayer(s, 0, 1, 3);
    s.players[0].hand = {MBCard("Lightning Bolt", 20), MBCard("Chandra's Incinerator", 21)};
    const auto plans = TurnSolver::EnumerateMainPlans(s, true);
    bool found = false;
    for (const auto& p : plans)
    {
        bool bolt = false, incinerator = false;
        for (const auto& a : p.actions)
        {
            bolt |= a.card_name == "Lightning Bolt";
            incinerator |= a.card_name == "Chandra's Incinerator";
        }
        found |= bolt && incinerator;
    }
    CHECK(found);
}

TEST_CASE("midrange burn: an offered Bolt and Skewer bundle labels any dropped cast")
{
    GameState s; s.active_player_index = 0; s.turn_number = 2;
    static const MidrangeBurnProvider provider; s.m_provider = &provider;
    MBPut(s, "Mountain", 10); MBPut(s, "Mountain", 11);
    s.players[0].hand = {MBCard("Skewer the Critics", 20), MBCard("Lightning Bolt", 21)};
    const auto plans = TurnSolver::EnumerateMainPlans(s, true);
    int offered = 0;
    for (const auto& p : plans)
    {
        bool skewer = false, bolt = false;
        for (const auto& a : p.actions)
        {
            skewer |= a.card_name == "Skewer the Critics";
            bolt |= a.card_name == "Lightning Bolt";
        }
        if (!skewer || !bolt) { continue; }
        ++offered;
        GameState copy = s;
        TurnSolver::ApplyPlan(copy, p, true);
        INFO("offered bundle " << offered << " retained " << copy.players[0].hand.size() << " cards");
        if (!copy.players[0].hand.empty())
        {
            CHECK(!p.would_drop.empty());
            CHECK(std::find(p.would_drop.begin(), p.would_drop.end(), "Skewer the Critics")
                  != p.would_drop.end());
        }
    }
    CHECK(offered > 0);
}

TEST_CASE("midrange burn: Searing Blaze cannot target a creature before it is cast")
{
    if (!HumanPlayActive()) { return; } // This is a searched-order human menu invariant.
    GameState s; s.active_player_index = 0; s.turn_number = 4;
    static const MidrangeBurnProvider provider; s.m_provider = &provider;
    for (int i = 0; i < 3; ++i) { MBPut(s, "Mountain", 10 + i); }
    s.players[0].hand = {MBCard("Searing Blaze", 20), MBCard("Monastery Swiftspear", 21)};
    const auto plans = TurnSolver::EnumerateMainPlans(s, true);
    bool legal_order = false, illegal_order = false;
    int illegal_searched = 0, illegal_canonical = 0;
    for (const auto& p : plans)
    {
        int blaze = -1, swiftspear = -1;
        FixedBurnPin pin;
        for (int i = 0; i < static_cast<int>(p.actions.size()); ++i)
        {
            const Action& a = p.actions[i];
            if (a.card_name == "Searing Blaze"
                && DecodeFixedBurnPin(a.tutor_target.str(), pin)
                && (pin.first == 21 || pin.second == 21)) { blaze = i; }
            if (a.card_name == "Monastery Swiftspear") { swiftspear = i; }
        }
        if (blaze < 0 || swiftspear < 0) { continue; }
        if (blaze < swiftspear)
        { illegal_order = true; (p.searched_order ? illegal_searched : illegal_canonical)++; }
        else { legal_order = true; }
    }
    INFO("illegal searched=" << illegal_searched << " canonical=" << illegal_canonical);
    CHECK(legal_order);
    CHECK_FALSE(illegal_order);
}

TEST_CASE("midrange burn: a burn plus Incinerator line does not prepay the unreduced bill")
{
    GameState s; s.active_player_index = 0; s.turn_number = 5;
    s.deck_tracks_noncombat_damage = true;
    static const MidrangeBurnProvider provider; s.m_provider = &provider;
    for (int i = 0; i < 3; ++i) { MBPut(s, "Mountain", 10 + i); }
    MBPut(s, "Fiery Islet", 13); MBPut(s, "Fiery Islet", 14);
    s.floating_mana.red = 2;
    s.players[0].hand = {MBCard("Lightning Bolt", 20), MBCard("Chandra's Incinerator", 21)};
    TurnSolver::Plan p;
    for (int i = 0; i < 2; ++i)
    {
        Action a; a.kind = Action::Kind::CastFromHand; a.hand_index = i;
        a.card_name = s.players[0].hand[i].m_name; a.def = &MBDef(a.card_name.str());
        a.cost = EffectiveSpellCost(*a.def, s, 1);
        a.card_mv = a.def->card.m_mana_cost.ManaValue();
        a.is_noncreature = !a.def->card.IsCreature();
        a.direct_damage = a.def->params.damage;
        p.actions.push_back(a);
    }
    auto probe = s;
    CHECK_FALSE(TurnSolver::BatchPrepayMainCasts(probe, p.actions));
    CHECK(probe.players[0].life == 20);
    CHECK(std::none_of(probe.battlefield.begin(), probe.battlefield.end(),
        [](const Permanent& q) { return q.tapped; }));
    TurnSolver::ApplyPlan(s, p, true);
    CHECK(s.players[1].life == 17);
    CHECK(s.players[0].life == 20);
    CHECK(s.players[0].hand.empty());
    CHECK_FALSE(s.battlefield[3].tapped);
    CHECK_FALSE(s.battlefield[4].tapped);
    CHECK(s.floating_mana.white == 0);
}

TEST_CASE("midrange burn: damage spell precedes Light Up in an opaque two-cast plan")
{
    GameState s; s.active_player_index = 0; s.turn_number = 2;
    static const MidrangeBurnProvider provider; s.m_provider = &provider;
    MBPut(s, "Mountain", 10); MBPut(s, "Mountain", 11);
    s.players[0].hand = {MBCard("Light Up the Stage", 20), MBCard("Lightning Bolt", 21)};
    s.players[0].library.push_back(MBCard("Mountain", 30));
    s.players[0].library.push_back(MBCard("Mountain", 31));
    TurnSolver::Plan p;
    for (int i = 0; i < 2; ++i)
    {
        Action a; a.kind = Action::Kind::CastFromHand; a.hand_index = i;
        a.card_name = s.players[0].hand[i].m_name; a.def = &MBDef(a.card_name.str());
        a.cost = EffectiveSpellCost(*a.def, s, 1);
        a.card_mv = a.def->card.m_mana_cost.ManaValue();
        a.is_noncreature = true; a.direct_damage = a.def->params.damage;
        a.has_spectacle = a.def->params.spectacle_cost.has_value();
        p.actions.push_back(a);
    }
    TurnSolver::ApplyPlan(s, p, true);
    CHECK(s.players[1].life == 17);
    CHECK(std::none_of(s.players[0].hand.begin(), s.players[0].hand.end(),
        [](const Card& c) { return c.m_name == "Light Up the Stage"; }));
    CHECK(s.battlefield[0].tapped);
    CHECK(s.battlefield[1].tapped);
}

TEST_CASE("midrange burn: Lavamancer enables same-plan spectacle before Light Up")
{
    GameState s; s.active_player_index = 0; s.turn_number = 3;
    static const MidrangeBurnProvider provider; s.m_provider = &provider;
    MBPut(s, "Grim Lavamancer", 1);
    MBPut(s, "Mountain", 10); MBPut(s, "Mountain", 11); MBPut(s, "Mountain", 12);
    s.players[0].graveyard = {MBCard("Lightning Bolt", 40), MBCard("Mountain", 41)};
    s.players[0].hand = {MBCard("Light Up the Stage", 20)};
    s.players[0].library.push_back(MBCard("Mountain", 30));
    s.players[0].library.push_back(MBCard("Mountain", 31));
    TurnSolver::Plan p;
    Action lava; lava.kind = Action::Kind::GraveyardExileAbility;
    lava.card_name = "Grim Lavamancer"; lava.def = &MBDef("Grim Lavamancer");
    lava.sac_source_id = 1; lava.gy_exile_mode = 3; lava.cost.red = 1;
    lava.tutor_target = GraveyardDamageFuelSpec("Lightning Bolt", "Mountain");
    lava.enchant_target = 0; lava.direct_damage = 2;
    p.actions.push_back(lava);
    Action light; light.kind = Action::Kind::CastFromHand; light.hand_index = 0;
    light.card_name = "Light Up the Stage"; light.def = &MBDef("Light Up the Stage");
    light.cost = EffectiveSpellCost(*light.def, s, 1); light.card_mv = 3;
    light.is_noncreature = true; light.has_spectacle = true;
    p.actions.push_back(light);
    TurnSolver::ApplyPlan(s, p, true);
    CHECK(s.players[1].life == 18);
    CHECK(s.players[0].graveyard.size() == 1); // Light Up; fuel was exiled
    CHECK(std::none_of(s.players[0].hand.begin(), s.players[0].hand.end(),
        [](const Card& c) { return c.m_name == "Light Up the Stage"; }));
    CHECK(s.battlefield[0].tapped);
    CHECK(s.battlefield[1].tapped);
    CHECK(s.battlefield[2].tapped);
    CHECK_FALSE(s.battlefield[3].tapped);
}

TEST_CASE("midrange burn: Searing Blaze target prompts report landfall damage")
{
    MBHooks restore;
    GameState s; s.active_player_index = 0; s.turn_number = 4;
    s.players[0].land_entered_this_turn = true;
    MBPut(s, "Grim Lavamancer", 101);
    int prompts = 0;
    TargetChooser check = [&](const GameState&, const CardDefinition& d, int, int,
                              int damage, const std::vector<ChosenTarget>& suggested)
    {
        CHECK(d.card.m_name == "Searing Blaze");
        CHECK(damage == 3);
        ++prompts;
        return suggested;
    };
    g_play_target_chooser = &check;
    FixedBurnPin selected;
    REQUIRE(PrepareFixedBurnPin(s, 0, MBDef("Searing Blaze"),
        EncodeFixedBurnPin({-1, 101, 0}), selected));
    CHECK(prompts == 2);
}

TEST_CASE("midrange burn: mandatory Incinerator trigger waits until the full damage spell finishes")
{
    MBHooks restore;
    GameState s; s.active_player_index = 0; s.deck_tracks_noncombat_damage = true;
    MBPut(s, "Chandra's Incinerator", 1);
    MBPut(s, "Goblin Guide", 2, 1); MBPut(s, "Chandra's Incinerator", 3, 1);
    int prompts = 0;
    TargetChooser choose = [&](const GameState& ss, const CardDefinition& def, int,
                               int, int amount, const std::vector<ChosenTarget>& defaults)
    {
        ++prompts; CHECK(def.params.incinerator_damage_trigger); CHECK(amount == 3);
        CHECK(std::none_of(ss.battlefield.begin(), ss.battlefield.end(), [](const Permanent& q)
            { return q.card.m_number == 2; })); // dead Blaze target already left at SBAs
        return defaults;
    };
    g_play_target_chooser = &choose;
    {
        NoncombatTriggerScope spell(s);
        s.players[1].life -= 3;
        RecordNoncombatDamageToPlayer(s, 0, 1, 3);
        CHECK(prompts == 0); // cannot resolve the trigger halfway through Searing Blaze
        s.battlefield[1].damage = 3;
    }
    CHECK(prompts == 1); CHECK(s.pending_incinerator_damage.empty());
    REQUIRE(s.battlefield.size() == 2);
    CHECK(s.battlefield[1].card.m_number == 3); CHECK(s.battlefield[1].damage == 3);
}

TEST_CASE("midrange burn: multiple Incinerators each trigger and a departed source's trigger survives")
{
    GameState s; s.active_player_index = 0; s.deck_tracks_noncombat_damage = true;
    MBPut(s, "Chandra's Incinerator", 1); MBPut(s, "Chandra's Incinerator", 2);
    MBPut(s, "Chandra's Incinerator", 3, 1);
    {
        NoncombatTriggerScope spell(s);
        RecordNoncombatDamageToPlayer(s, 0, 1, 2);
        REQUIRE(s.pending_incinerator_damage.size() == 2);
        s.battlefield.erase(s.battlefield.begin()); // source leaving cannot remove its trigger
    }
    REQUIRE(s.battlefield.size() == 2); CHECK(s.battlefield[1].damage == 4);
}

TEST_CASE("midrange burn: Incinerator trigger cannot target our own creature or a player")
{
    MBHooks restore;
    GameState s; s.active_player_index = 0; s.deck_tracks_noncombat_damage = true;
    MBPut(s, "Chandra's Incinerator", 1); MBPut(s, "Goblin Guide", 2);
    int prompts = 0;
    TargetChooser chooser = [&](const GameState&, const CardDefinition&, int, int, int,
                                const std::vector<ChosenTarget>& defaults) { ++prompts; return defaults; };
    g_play_target_chooser = &chooser;
    RecordNoncombatDamageToPlayer(s, 0, 1, 3);
    CHECK(prompts == 0); CHECK(s.battlefield[0].damage == 0); CHECK(s.battlefield[1].damage == 0);
}

TEST_CASE("midrange burn: Incinerator damage removes loyalty from an opposing planeswalker")
{
    GameState s; s.active_player_index = 0; s.deck_tracks_noncombat_damage = true;
    MBPut(s, "Chandra's Incinerator", 1); MBPut(s, "Gideon, Ally of Zendikar", 2, 1);
    s.battlefield[1].loyalty = 4;
    RecordNoncombatDamageToPlayer(s, 0, 1, 3);
    CHECK(s.battlefield[1].loyalty == 1); CHECK(s.battlefield[1].damage == 0);
    RecordNoncombatDamageToPlayer(s, 0, 1, 1);
    CHECK(s.battlefield.size() == 1); CHECK(s.players[1].graveyard.size() == 1);
}

TEST_CASE("midrange burn: payment rollback restores noncombat counters and pending trigger queue")
{
    GameState s; s.active_player_index = 0; s.deck_tracks_noncombat_damage = true;
    MBPut(s, "Chandra's Incinerator", 1);
    {
        NoncombatTriggerScope spell(s);
        RecordNoncombatDamageToPlayer(s, 0, 1, 2);
        NoncombatStateSnapshot before(s);
        RecordNoncombatDamageToPlayer(s, 0, 1, 3);
        CHECK(s.noncombat_damage_received_this_turn[1] == 5);
        before.Restore(s);
        CHECK(s.noncombat_damage_received_this_turn[1] == 2);
        CHECK(s.pending_incinerator_damage.size() == 1);
    }
}

TEST_CASE("midrange burn: searched Incinerator target pins choose distinct live opponent permanents")
{
    struct FullTargets : GenericProvider
    {
        void IncineratorTargetCandidates(const GameState& s, int who, int,
                                          std::vector<int>& out) const override
        {
            out.clear();
            for (const Permanent& p : s.battlefield)
            { if (p.controller_index == who && p.card.IsCreature()) { out.push_back(p.card.m_number); } }
        }
    } provider;
    GameState s; s.active_player_index = 0; s.turn_number = 3;
    s.deck_tracks_noncombat_damage = true; s.m_provider = &provider;
    MBPut(s, "Chandra's Incinerator", 1); MBPut(s, "Chandra's Incinerator", 2, 1);
    MBPut(s, "Chandra's Incinerator", 3, 1); MBPut(s, "Mountain", 4);
    s.players[0].hand = {MBCard("Lightning Bolt", 5)};
    const auto plans = TurnSolver::EnumerateMainPlans(s, false);
    bool saw_first = false, saw_second = false;
    for (const auto& p : plans)
    {
        if (p.incinerator_targets.empty()) { continue; }
        saw_first |= p.incinerator_targets.front() == 2;
        saw_second |= p.incinerator_targets.front() == 3;
        GameState copy = s;
        TurnSolver::ApplyPlan(copy, p, false);
        for (const Permanent& q : copy.battlefield)
        {
            if (q.controller_index == 1)
            { CHECK(q.damage == (q.card.m_number == p.incinerator_targets.front() ? 3 : 0)); }
        }
    }
    CHECK(saw_first); CHECK(saw_second);
}


TEST_CASE("midrange burn: nested Incinerator target scopes isolate and restore probe ownership")
{
    std::vector<int> outer_pins, inner_pins;
    int outer_calls = 0, inner_calls = 0;
    IncineratorTargetProbe outer = [&](const std::vector<int>&) { ++outer_calls; };
    IncineratorTargetProbe inner = [&](const std::vector<int>&) { ++inner_calls; };
    {
        IncineratorTargetProbeScope parent(outer, outer_pins, 0);
        IncineratorPinScope parent_pins(outer_pins);
        REQUIRE(g_incinerator_target_probe == &outer);
        (*g_incinerator_target_probe)({101});
        g_incinerator_pin_cursor = 1;
        {
            IncineratorPinScope speculative(inner_pins);
            CHECK(g_incinerator_target_probe == nullptr);
            CHECK(g_incinerator_probe_owner == nullptr);
            {
                IncineratorTargetProbeScope nested(inner, inner_pins, 1);
                IncineratorPinScope nested_pins(inner_pins);
                REQUIRE(g_incinerator_target_probe == &inner);
                (*g_incinerator_target_probe)({202});
            }
            CHECK(g_incinerator_target_probe == nullptr);
        }
        REQUIRE(g_incinerator_target_probe == &outer);
        CHECK(g_incinerator_probe_owner == &outer_pins);
        CHECK(g_incinerator_pin_cursor == 1);
        CHECK(g_incinerator_probe_fan_depth == 0);
        (*g_incinerator_target_probe)({303});
    }
    CHECK(g_incinerator_target_probe == nullptr);
    CHECK(g_incinerator_probe_owner == nullptr);
    CHECK(outer_calls == 2); CHECK(inner_calls == 1);
}


TEST_CASE("midrange burn: free Fireblast damage funds Incinerator in sequential feasibility")
{
    GameState s; s.active_player_index = 0; s.turn_number = 3;
    s.deck_tracks_noncombat_damage = true;
    MBPut(s, "Mountain", 101, 0, true); MBPut(s, "Mountain", 102, 0, true);
    MBPut(s, "Mountain", 103);
    s.floating_mana.colorless = 1;
    s.players[0].hand = {MBCard("Fireblast", 201), MBCard("Chandra's Incinerator", 202)};
    Action fire; fire.kind = Action::Kind::CastFromHand; fire.card_name = "Fireblast";
    fire.hand_index = 0; fire.def = &MBDef("Fireblast"); fire.direct_damage = 4;
    fire.is_noncreature = true; fire.card_mv = 6; fire.tutor_target = "fireblast:101,102";
    Action inc; inc.kind = Action::Kind::CastFromHand; inc.card_name = "Chandra's Incinerator";
    inc.hand_index = 1; inc.def = &MBDef("Chandra's Incinerator"); inc.card_mv = 6;
    inc.cost = EffectiveSpellCost(*inc.def, s, 1);
    CHECK(TurnSolver::OrderPaysSequentially(s, {fire, inc}, {0, 1}));
    CHECK_FALSE(TurnSolver::OrderPaysSequentially(s, {fire, inc}, {1, 0}));
    CHECK(s.noncombat_damage_received_this_turn[1] == 0); // scratch never leaks resources
    CHECK(s.battlefield.size() == 3);
}



TEST_CASE("midrange burn: prevented damage never becomes Incinerator's noncombat resource")
{
    GameState s; s.active_player_index = 0; s.deck_tracks_noncombat_damage = true;
    s.dmg_events_armed = true;
    MBPut(s, "Chandra's Incinerator", 1); MBPut(s, "Purity", 2);
    REQUIRE(DealPermanentAbilityDamage(s, 0, MBDef("Lightning Bolt"), 3, kAbilitySelfTarget, false));
    CHECK(s.players[0].life == 23);
    CHECK(s.noncombat_damage_received_this_turn[0] == 0);
    CHECK(s.noncombat_damage_received_this_turn[1] == 0);
    CHECK(s.pending_incinerator_damage.empty());
}

TEST_CASE("midrange burn: armed cast-trigger damage records actual damage and triggers opposing Incinerator")
{
    GameState s; s.active_player_index = 0; s.deck_tracks_noncombat_damage = true;
    s.dmg_events_armed = true;
    MBPut(s, "Eidolon of the Great Revel", 1, 1);
    MBPut(s, "Chandra's Incinerator", 2, 1); MBPut(s, "Goblin Guide", 3);
    FireOnCastTriggers(s, MBDef("Lightning Bolt"));
    CHECK(s.players[0].life == 18);
    CHECK(s.noncombat_damage_received_this_turn[0] == 2);
    CHECK(s.noncombat_damage_received_this_turn[1] == 0);
    CHECK(std::none_of(s.battlefield.begin(), s.battlefield.end(), [](const Permanent& p)
        { return p.card.m_number == 3; })); // the opponent's mandatory trigger resolves
}



TEST_CASE("midrange burn: last-life Islet payment ends the game before Bolt resolves in both worlds")
{
    GameState base; base.active_player_index = 0; base.turn_number = 3;
    base.own_death_live = true; base.players[0].life = 1; base.players[1].life = 3;
    MBPut(base, "Fiery Islet", 971);
    base.players[0].hand.push_back(MBCard("Lightning Bolt", 972));

    GameState real = base;
    AIEngine ai;
    ManaPool available = AvailableManaPool(real, nullptr);
    MtgTestSeam::CastSpellFromHand(ai, real, real.players[0].hand.front(), available, 0);
    CHECK(real.players[0].life == 0);
    CHECK(real.spells_cast_this_turn == 1);
    CHECK(real.players[0].hand.empty());
    REQUIRE(real.stack.size() == 1);
    StackEntry entry = real.stack.back(); real.stack.pop_back();
    CHECK_FALSE(EffectHandler::Resolve(real, entry, MBDef("Lightning Bolt")));
    CHECK(real.players[1].life == 3);

    GameState rollout = base;
    TurnSolver::Plan plan; Action cast; cast.kind = Action::Kind::CastFromHand;
    cast.card_name = "Lightning Bolt"; cast.hand_index = 0;
    plan.actions.push_back(cast);
    TurnSolver::ApplyPlan(rollout, plan, true);
    CHECK(rollout.players[0].life <= 0);
    CHECK(rollout.players[1].life == 3);
    CHECK(rollout.spells_cast_this_turn == 1);
}

TEST_CASE("midrange burn: nested cast flushes emblem Incinerator trigger above Magma Jet")
{
    MBHooks restore;
    GameState s; s.active_player_index = 0; s.turn_number = 4;
    static const MidrangeBurnProvider provider;
    s.m_provider = &provider;
    s.deck_tracks_noncombat_damage = true; s.own_death_live = true;
    s.players[0].emblem_spell_cast_damage = 5;
    MBPut(s, "Chandra's Incinerator", 981);
    MBPut(s, "Mountain", 982); MBPut(s, "Mountain", 983);
    MBPut(s, "Monastery Swiftspear", 984, 1);
    s.battlefield.back().temp_tough_bonus = 1; // a three-toughness opposing creature
    s.players[0].hand.push_back(MBCard("Magma Jet", 985));
    s.players[0].library = {};

    for (const Card& c : {MBCard("Mountain", 986), MBCard("Lightning Bolt", 987)}) { s.players[0].library.push_back(c); }
    int target_prompts = 0; bool scry = false;
    TargetChooser chooser = [&](const GameState& live, const CardDefinition& d, int, int, int,
                                const std::vector<ChosenTarget>&) -> std::vector<ChosenTarget>
    {
        ++target_prompts;
        if (d.card.m_name.str() == "Chandra emblem") { return {{0, 1, 0}}; }
        for (int i = 0; i < static_cast<int>(live.battlefield.size()); ++i)
        { if (live.battlefield[i].card.m_number == 984) { return {{1, i, 0}}; } }
        return {};
    };
    TopChooser top = [&](const GameState&, const std::string&, const std::vector<Card>&, LookKind)
    { scry = true; return TopDisposition{}; };
    g_play_target_chooser = &chooser; g_play_top_chooser = &top;
    AIEngine ai;
    {
        // Reproduce a cast made while Chandra's +1 resolution batches its own triggers.
        NoncombatTriggerScope chandra_resolution(s);
        ManaPool available = AvailableManaPool(s, nullptr);
        MtgTestSeam::CastSpellFromHand(ai, s, s.players[0].hand.front(), available, 0);
        CHECK(s.noncombat_trigger_depth == 1); // the priority flush restores the outer scope
        CHECK(s.pending_incinerator_damage.empty());
        bool creature_alive = false;
        for (const Permanent& p : s.battlefield) { creature_alive |= p.card.m_number == 984; }
        CHECK_FALSE(creature_alive);
        REQUIRE(s.stack.size() == 1);
        StackEntry entry = s.stack.back(); s.stack.pop_back();
        REQUIRE(EffectHandler::Resolve(s, entry, MBDef("Magma Jet")));
    }
    CHECK(target_prompts == 3); // Jet target, emblem target, Incinerator target
    CHECK_FALSE(scry);
    CHECK(s.players[1].life == 15);
    CHECK(s.players[0].library.size() == 2);
}


TEST_CASE("midrange burn: lethal cast triggers stop free and suspended burn resolution")
{
    MBHooks restore;
    GameState free; free.active_player_index = 0; free.own_death_live = true;
    free.players[0].life = 2;
    MBPut(free, "Eidolon of the Great Revel", 991);
    REQUIRE(EffectHandler::PushFreeCast(free, MBCard("Lightning Bolt", 992), 0));
    CHECK(free.spells_cast_this_turn == 1);
    CHECK(free.players[0].life <= 0);
    REQUIRE(free.stack.size() == 1);
    StackEntry entry = free.stack.back(); free.stack.pop_back();
    CHECK_FALSE(EffectHandler::Resolve(free, entry, MBDef("Lightning Bolt")));
    CHECK(free.players[1].life == 20);

    GameState suspended; suspended.active_player_index = 0; suspended.turn_number = 2;
    suspended.own_death_live = true; suspended.players[0].life = 2;
    MBPut(suspended, "Eidolon of the Great Revel", 993);
    suspended.players[0].suspended_cards.push_back({MBCard("Rift Bolt", 994), 2});
    FreeCastChooser take = [](const GameState&, int, const std::string&, const std::vector<Card>&,
                              int, const std::vector<std::string>&) { return 0; };
    g_play_free_cast_chooser = &take; // human deliberately takes the lethal cast
    ProcessSuspendArrivals(suspended, 0);
    CHECK(suspended.spells_cast_this_turn == 1);
    CHECK(suspended.players[0].life <= 0);
    CHECK(suspended.players[1].life == 20);
}


TEST_CASE("midrange burn: each Chandra emblem's Incinerator trigger resolves before the next emblem")
{
    MBHooks restore;
    GameState s; s.active_player_index = 0; s.deck_tracks_noncombat_damage = true;
    s.players[0].emblem_spell_cast_damage = 10;
    MBPut(s, "Chandra's Incinerator", 1); MBPut(s, "Goblin Guide", 2, 1);
    MBPut(s, "Chandra's Incinerator", 3, 1);
    int emblem_prompts = 0, inc_prompts = 0;
    TargetChooser choose = [&](const GameState& ss, const CardDefinition& d, int, int, int,
                               const std::vector<ChosenTarget>& defaults)
    {
        if (d.card.m_name == "Chandra emblem")
        {
            ++emblem_prompts;
            if (emblem_prompts == 2)
            { CHECK(std::none_of(ss.battlefield.begin(), ss.battlefield.end(), [](const Permanent& p)
                { return p.card.m_number == 2; })); }
            return defaults;
        }
        ++inc_prompts;
        const int id = inc_prompts == 1 ? 2 : 3;
        for (int i = 0; i < static_cast<int>(ss.battlefield.size()); ++i)
        { if (ss.battlefield[i].card.m_number == id) { return std::vector<ChosenTarget>{{1, i, 0}}; } }
        return defaults;
    };
    g_play_target_chooser = &choose;
    { NoncombatTriggerScope outer_loyalty_resolution(s);
      FireOnCastTriggers(s, MBDef("Lightning Bolt"));
      CHECK(s.noncombat_trigger_depth == 1); }
    CHECK(emblem_prompts == 2); CHECK(inc_prompts == 2);
    CHECK(s.players[1].life == 10);
    CHECK(s.noncombat_damage_received_this_turn[1] == 10);
}
