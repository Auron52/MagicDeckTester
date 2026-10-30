#include <doctest/doctest.h>
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/SpellEffects.h"
#include "core/HeuristicDefaults.h"
#include "ai/TurnSolver.h"

namespace {
const CardDefinition& BurnDef(const std::string& name)
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const auto* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    return *d;
}
GameState LavamancerState()
{
    GameState s;
    s.active_player_index = 0;
    s.players[0].life = s.players[1].life = 20;
    Permanent p; p.card = BurnDef("Grim Lavamancer").card; p.card.m_number = 101;
    p.controller_index = p.owner_index = 0;
    s.battlefield.push_back(p);
    s.players[0].graveyard.push_back(BurnDef("Lightning Bolt").card);
    s.players[0].graveyard.push_back(BurnDef("Mountain").card);
    return s;
}
std::string Fuel() { return GraveyardDamageFuelSpec("Lightning Bolt", "Mountain"); }
}
TEST_CASE("Lavamancer pays two unrestricted exile costs and deals damage once")
{
    auto s = LavamancerState();
    int payments = 0;
    const MadnessPayFn pay = [&](GameState&, const ManaCost& c) {
        CHECK(c.red == 1); CHECK(c.ManaValue() == 1); ++payments; return true;
    };
    REQUIRE(ApplyGraveyardDamageAbility(s, 0, 101, Fuel(), 0, pay));
    CHECK(s.players[1].life == 18);
    CHECK(s.players[0].graveyard.empty());
    CHECK(s.exile.size() == 2);
    CHECK(s.battlefield[0].tapped);
    CHECK(payments == 1);
    CHECK_FALSE(ApplyGraveyardDamageAbility(s, 0, 101, Fuel(), 0, pay));
    CHECK(payments == 1);
}
TEST_CASE("Lavamancer rejects sickness and missing fuel before paying mana")
{
    auto s = LavamancerState();
    int payments = 0;
    const MadnessPayFn pay = [&](GameState&, const ManaCost&) { ++payments; return true; };
    s.battlefield[0].entered_this_turn = true;
    CHECK_FALSE(ApplyGraveyardDamageAbility(s, 0, 101, Fuel(), 0, pay));
    s.battlefield[0].entered_this_turn = false;
    s.players[0].graveyard.pop_back();
    CHECK_FALSE(ApplyGraveyardDamageAbility(s, 0, 101, Fuel(), 0, pay));
    CHECK(payments == 0);
    CHECK(s.players[0].graveyard.size() == 1);
    CHECK_FALSE(s.battlefield[0].tapped);
}
TEST_CASE("Lavamancer failed mana payment leaves exile and tap costs untouched")
{
    auto s = LavamancerState();
    const MadnessPayFn pay = [](GameState&, const ManaCost&) { return false; };
    CHECK_FALSE(ApplyGraveyardDamageAbility(s, 0, 101, Fuel(), 0, pay));
    CHECK(s.players[0].graveyard.size() == 2);
    CHECK(s.exile.empty());
    CHECK_FALSE(s.battlefield[0].tapped);
    CHECK(s.players[1].life == 20);
}
TEST_CASE("Lavamancer can select own creature or own player without opponent damage")
{
    auto s = LavamancerState();
    const MadnessPayFn pay = [](GameState&, const ManaCost&) { return true; };
    REQUIRE(ApplyGraveyardDamageAbility(s, 0, 101, Fuel(), kAbilitySelfTarget, pay));
    CHECK(s.players[0].life == 18);
    CHECK(s.players[1].life == 20);
    s = LavamancerState();
    REQUIRE(ApplyGraveyardDamageAbility(s, 0, 101, Fuel(), 101, pay));
    CHECK(s.battlefield.empty());
    CHECK(s.players[0].graveyard.size() == 1);
    CHECK(s.players[1].life == 20);
}
TEST_CASE("Lavamancer fuel pair requires two physical copies for equal names")
{
    auto s = LavamancerState();
    const std::string pair = GraveyardDamageFuelSpec("Lightning Bolt", "Lightning Bolt");
    CHECK(GraveyardDamageFuelSlots(s, 0, pair).empty());
    s.players[0].graveyard.push_back(BurnDef("Lightning Bolt").card);
    const auto slots = GraveyardDamageFuelSlots(s, 0, pair);
    REQUIRE(slots.size() == 2);
    CHECK(slots[0] != slots[1]);
}
TEST_CASE("Swiftspear prowess is a noncreature cast trigger and haste is retained")
{
    auto s = LavamancerState();
    s.battlefield[0].card = BurnDef("Monastery Swiftspear").card;
    REQUIRE(s.battlefield[0].card.HasKeyword(Keyword::Haste));
    FireProwess(s, BurnDef("Grim Lavamancer"));
    CHECK(s.battlefield[0].temp_power_bonus == 0);
    FireProwess(s, BurnDef("Lightning Bolt"));
    CHECK(s.battlefield[0].temp_power_bonus == 1);
    CHECK(s.battlefield[0].temp_tough_bonus == 1);
}

namespace {
void PutChandra(GameState& s, int loyalty = 4)
{
    Permanent p; p.card = BurnDef("Chandra, Torch of Defiance").card; p.card.m_number = 102;
    p.controller_index = p.owner_index = 0; p.loyalty = loyalty;
    s.battlefield.push_back(p);
}
}
TEST_CASE("Lavamancer fuel identity is content-stable across unrelated prior pairs")
{
    const auto expected = GraveyardDamageFuelSpec("Mountain", "Lightning Bolt");
    for (int i = 0; i < 100; ++i)
    { GraveyardDamageFuelSpec("unrelated" + std::to_string(i), "fuel"); }
    CHECK(expected == GraveyardDamageFuelSpec("Lightning Bolt", "Mountain"));
    CHECK(expected == "Lightning Bolt\nMountain");
}
TEST_CASE("Chandra first plus exiles lands and deals damage instead of playing them")
{
    auto s = LavamancerState(); PutChandra(s);
    s.players[0].library.push_back(BurnDef("Mountain").card);
    const auto lands = s.players[0].lands_played_this_turn;
    ApplyLoyaltyAbility(s, 0, 102, 0);
    CHECK(s.players[1].life == 20 - 2 * gamesetup::OpponentHeads());
    CHECK(s.players[0].library.empty());
    CHECK(s.exile.size() == 1);
    CHECK(s.players[0].lands_played_this_turn == lands);
    CHECK(s.battlefield.back().loyalty == 5);
}
TEST_CASE("Chandra first plus optional paid cast is immediate or stays exiled")
{
    auto s = LavamancerState(); PutChandra(s);
    s.players[0].library.push_back(BurnDef("Lightning Bolt").card);
    int casts = 0;
    const PaidExileCastFn host = [&](GameState& live, int owner, const Card& card,
                                     const std::string& option, bool probe) {
        CHECK(owner == 0); CHECK(card.m_name.str() == "Lightning Bolt"); CHECK(option == "paid-option");
        if (!probe) { ++casts; ++live.spells_cast_this_turn; live.players[owner].graveyard.push_back(card); }
        return true;
    };
    const PaidExileCastScope scope(&host);
    ApplyLoyaltyAbility(s, 0, 102, 0, 0, "paid-option", true);
    CHECK(casts == 1); CHECK(s.exile.empty());
    CHECK(s.players[1].life == 20);
    s = LavamancerState(); PutChandra(s);
    s.players[0].library.push_back(BurnDef("Lightning Bolt").card);
    ApplyLoyaltyAbility(s, 0, 102, 0, 0, "paid-option", false);
    CHECK(casts == 1); CHECK(s.exile.size() == 1);
    CHECK(s.players[1].life == 20 - 2 * gamesetup::OpponentHeads());
}
TEST_CASE("Chandra failed paid cast and empty library take the printed damage branch")
{
    auto s = LavamancerState(); PutChandra(s);
    s.players[0].library.push_back(BurnDef("Lightning Bolt").card);
    const PaidExileCastFn host = [](GameState&, int, const Card&, const std::string&, bool) { return false; };
    const PaidExileCastScope scope(&host);
    ApplyLoyaltyAbility(s, 0, 102, 0, 0, "cannot-pay", true);
    CHECK(s.exile.size() == 1); CHECK(s.players[1].life == 20 - 2 * gamesetup::OpponentHeads());
    s = LavamancerState(); PutChandra(s);
    ApplyLoyaltyAbility(s, 0, 102, 0);
    CHECK(s.exile.empty()); CHECK(s.players[1].life == 20 - 2 * gamesetup::OpponentHeads());
}
TEST_CASE("Chandra second plus adds red but remains limited to one loyalty activation")
{
    auto s = LavamancerState(); PutChandra(s);
    ApplyLoyaltyAbility(s, 0, 102, 1);
    CHECK(s.floating_mana.red == 2); CHECK(s.battlefield.back().loyalty == 5);
    ApplyLoyaltyAbility(s, 0, 102, 1);
    CHECK(s.floating_mana.red == 2); CHECK(s.battlefield.back().loyalty == 5);
}
TEST_CASE("Chandra minus three can kill our own creature and requires a target before loyalty cost")
{
    auto s = LavamancerState(); PutChandra(s);
    ApplyLoyaltyAbility(s, 0, 102, 2, 101);
    REQUIRE(s.battlefield.size() == 1);
    CHECK(s.battlefield[0].loyalty == 1);
    CHECK(s.players[0].graveyard.back().m_name.str() == "Grim Lavamancer");
    s = LavamancerState(); PutChandra(s);
    ApplyLoyaltyAbility(s, 0, 102, 2, 999999);
    CHECK(s.battlefield.back().loyalty == 4);
    CHECK_FALSE(s.battlefield.back().loyalty_activated_this_turn);
}
TEST_CASE("Chandra emblem survives loyalty death and stacks on every spell cast")
{
    auto s = LavamancerState(); PutChandra(s, 7);
    ApplyLoyaltyAbility(s, 0, 102, 3);
    CHECK(s.players[0].emblem_spell_cast_damage == 5);
    REQUIRE(s.battlefield.size() == 1); // Chandra dies to the loyalty cost
    FireOnCastTriggers(s, BurnDef("Grim Lavamancer"));
    CHECK(s.players[1].life == 15);
    s.players[0].emblem_spell_cast_damage += 5;
    FireOnCastTriggers(s, BurnDef("Lightning Bolt"));
    CHECK(s.players[1].life == 5);
}

TEST_CASE("Chandra nested paid action identity is independent of previous enumerations")
{
    auto s = LavamancerState(); PutChandra(s);
    Permanent land; land.card = BurnDef("Mountain").card; land.card.m_number = 103;
    land.controller_index = land.owner_index = 0; s.battlefield.push_back(land);
    s.players[0].library.push_back(BurnDef("Lightning Bolt").card);
    auto options = [](const GameState& state) {
        std::vector<std::string> result;
        for (const Action& a : TurnSolver::CollectActionsForTest(state, true))
        { if (a.kind == Action::Kind::ActivateLoyalty && a.loyalty_ability == 0 && a.ponder_keep == 1)
          { result.push_back(a.tutor_target.str()); } }
        return result;
    };
    const auto before = options(s);
    REQUIRE_FALSE(before.empty());
    auto other = s; other.players[0].library = {};
    other.players[0].library.push_back(BurnDef("Monastery Swiftspear").card);
    options(other);
    CHECK(options(s) == before);
    const Action decoded = TurnSolver::ChandraNestedCastOption(before[0]);
    CHECK(decoded.card_name.str() == "Lightning Bolt");
    CHECK(decoded.def == &BurnDef("Lightning Bolt"));
}

TEST_CASE("Lavamancer surfaces any target and both mandatory exile costs before payment")
{
    auto s = LavamancerState();
    std::vector<std::string> order;
    TargetChooser target = [&](const GameState& live, const CardDefinition& def, int,
                               int max_targets, int amount, const std::vector<ChosenTarget>&) {
        order.push_back("target"); CHECK(live.players[0].graveyard.size() == 2);
        CHECK_FALSE(live.battlefield[0].tapped); CHECK(def.params.targeting == Targeting::Any);
        CHECK(max_targets == 1); CHECK(amount == 2);
        return std::vector<ChosenTarget>{{0, 1, 0}};
    };
    GyReturnOneChooser fuel = [&](const GameState&, int, const std::string& source,
                                  const std::string& alternative, const std::vector<Card>& cards, int) {
        order.push_back("fuel" + std::to_string(g_graveyard_exile_cost_prompt));
        CHECK(source == "Grim Lavamancer"); CHECK(alternative.empty());
        CHECK(cards.size() == static_cast<std::size_t>(3 - g_graveyard_exile_cost_prompt));
        return 1;
    };
    struct Hooks {
        TargetChooser* t = g_play_target_chooser;
        GyReturnOneChooser* f = g_play_gy_return_one_chooser;
        ~Hooks() { g_play_target_chooser = t; g_play_gy_return_one_chooser = f; }
    } hooks;
    g_play_target_chooser = &target; g_play_gy_return_one_chooser = &fuel;
    const MadnessPayFn pay = [&](GameState&, const ManaCost&) { order.push_back("pay"); return true; };
    REQUIRE(ApplyGraveyardDamageAbility(s, 0, 101, Fuel(), 0, pay));
    const std::vector<std::string> expected{"target", "fuel1", "fuel2", "pay"};
    CHECK(order == expected);
    CHECK(g_graveyard_exile_cost_prompt == 0);
}
TEST_CASE("Chandra human paid cast prompt is flagged and restores its protocol scope")
{
    auto s = LavamancerState(); PutChandra(s);
    Permanent land; land.card = BurnDef("Mountain").card; land.card.m_number = 103;
    land.controller_index = land.owner_index = 0; s.battlefield.push_back(land);
    s.players[0].library.push_back(BurnDef("Lightning Bolt").card);
    int asked = 0;
    FreeCastChooser chooser = [&](const GameState&, int, const std::string& source,
                                  const std::vector<Card>& cards, int, const std::vector<std::string>&) {
        ++asked; CHECK(g_paid_exile_cast_prompt); CHECK(source == "Chandra, Torch of Defiance");
        REQUIRE(cards.size() == 1); CHECK(cards[0].m_name.str() == "Lightning Bolt");
        return -1;
    };
    struct Hook {
        FreeCastChooser* saved = g_play_free_cast_chooser;
        ~Hook() { g_play_free_cast_chooser = saved; }
    } hook;
    g_play_free_cast_chooser = &chooser;
    const PaidExileCastFn host = [](GameState&, int, const Card&, const std::string&, bool) { return true; };
    const PaidExileCastScope scope(&host);
    ApplyLoyaltyAbility(s, 0, 102, 0, 0, "payable", true);
    CHECK(asked == 1); CHECK_FALSE(g_paid_exile_cast_prompt);
    CHECK(s.players[1].life == 20 - 2 * gamesetup::OpponentHeads());
    CHECK(s.exile.size() == 1);
}
TEST_CASE("Chandra minus and emblem human targets use their rules-legal chooser")
{
    auto s = LavamancerState(); PutChandra(s);
    int minus_asked = 0;
    LoyaltyTargetChooser minus = [&](const GameState& live, int, const std::string&, const std::string&,
                                     const std::vector<int>& legal, int) {
        ++minus_asked; REQUIRE(legal.size() == 1);
        CHECK(live.battlefield[legal[0]].card.m_name.str() == "Grim Lavamancer");
        return 0;
    };
    struct Hooks {
        LoyaltyTargetChooser* l = g_play_loyalty_chooser;
        TargetChooser* t = g_play_target_chooser;
        ~Hooks() { g_play_loyalty_chooser = l; g_play_target_chooser = t; }
    } hooks;
    g_play_loyalty_chooser = &minus;
    ApplyLoyaltyAbility(s, 0, 102, 2);
    CHECK(minus_asked == 1); REQUIRE(s.battlefield.size() == 1);
    g_play_loyalty_chooser = nullptr;
    int emblem_asked = 0;
    TargetChooser emblem = [&](const GameState&, const CardDefinition& def, int,
                                int, int damage, const std::vector<ChosenTarget>&) {
        ++emblem_asked; CHECK(def.card.m_name.str() == "Chandra emblem");
        CHECK(def.params.targeting == Targeting::Any); CHECK(damage == 5);
        return std::vector<ChosenTarget>{{0, 0, 0}}; // our own face remains a legal choice
    };
    g_play_target_chooser = &emblem;
    s.players[0].emblem_spell_cast_damage = 5;
    FireOnCastTriggers(s, BurnDef("Lightning Bolt"));
    CHECK(emblem_asked == 1); CHECK(s.players[0].life == 15); CHECK(s.players[1].life == 20);
}

TEST_CASE("Chandra exile Fireblast offers only payable modes and distinguishes equal card names")
{
    auto setup = [](int mountains) {
        auto state = LavamancerState(); PutChandra(state);
        for (int i = 0; i < mountains; ++i)
        {
            Permanent p; p.card = BurnDef("Mountain").card; p.card.m_number = 200 + i;
            p.controller_index = p.owner_index = 0; state.battlefield.push_back(p);
        }
        return state;
    };
    const Card fireblast = BurnDef("Fireblast").card;
    auto three = setup(3);
    const auto alt_only = CollectChandraPaidCastOptions(three, 0, fireblast);
    REQUIRE(alt_only.size() == 1);
    CHECK(alt_only[0].cost_label == "Sacrifice two Mountains");
    auto six = setup(6);
    const auto both = CollectChandraPaidCastOptions(six, 0, fireblast);
    REQUIRE(both.size() == 2);
    CHECK(both[0].card.m_name == both[1].card.m_name);
    CHECK(both[0].cost_label != both[1].cost_label);
    CHECK(both[0].encoded != both[1].encoded);
    bool paid = false, alternative = false;
    for (const auto& option : both)
    {
        paid |= option.cost_label == "Pay {4}{R}{R}";
        alternative |= option.cost_label == "Sacrifice two Mountains";
        CHECK(ChandraPaidCastOptionPayable(six, 0, fireblast, option.encoded));
    }
    CHECK(paid); CHECK(alternative);
    CHECK(six.battlefield.size() == 8); // probing spends no real resources
    CHECK(six.spells_cast_this_turn == 0);
}

TEST_CASE("Chandra plan menu omits an exiled paid cast that cannot be funded")
{
    auto s = LavamancerState(); PutChandra(s);
    Permanent islet; islet.card = BurnDef("Fiery Islet").card; islet.card.m_number = 211;
    islet.controller_index = islet.owner_index = 0; s.battlefield.push_back(islet);
    s.players[0].library.push_back(BurnDef("Searing Blaze").card);
    int decline = 0, paid = 0;
    for (const Action& a : TurnSolver::CollectActionsForTest(s, true))
    {
        if (a.kind != Action::Kind::ActivateLoyalty || a.sac_source_id != 102
            || a.loyalty_ability != 0) { continue; }
        if (a.ponder_keep == 0) { ++decline; }
        else { ++paid; }
    }
    CHECK(decline == 1);
    CHECK(paid == 0);
}
