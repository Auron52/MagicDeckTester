// Boars I3b (2026-11-13): Raggadragga, Goreguts Boss -- "Each creature you control with a mana ability gets +2/+2.
// / Whenever a creature you control with a mana ability attacks, untap it. / Whenever you cast a spell, if at least
// seven mana was spent to cast it, untap target creature. It gets +7/+7 and gains trample until end of turn."
// The anthem (ComputeLordBonus on CreatureHasManaAbility -- HAS, not can-use), the declared-attacker untap (the Jorn
// shape, UntapSecondMainLive's third clause), the intervening-if cast trigger off FireOnCastTriggers' mana_spent,
// its searched creature target (the cast-trigger target rank), and the refund breakpoint (site 9, kActUntapRefund).
#include <doctest/doctest.h>
#include "ai/AIEngine.h"
#include "ai/Combat.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "core/EffectHandler.h"
#include "runner/GoldFishRunner.h"
#include "mtg_test_seam.h"
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{
const CardDefinition& RDef(const char* name)
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
Card RCard(const char* name, int number)
{
    Card c = RDef(name).card;
    c.m_number = number;
    return c;
}
int RPut(GameState& s, const char* name, int number, int controller = 0, bool sick = false)
{
    Permanent p;
    p.card = RCard(name, number);
    p.controller_index = p.owner_index = controller;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}
GameState RBase()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    s.deck_has_attack_untap = true;
    s.deck_has_cast_target_trigger = true;
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(RCard("Forest", 900 + k)); }
    return s;
}
const Permanent& RNum(const GameState& s, int num)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == num) { return p; } }
    FAIL("no permanent " << num);
    return s.battlefield.front();
}
Action RCast(const char* name)
{
    Action a;
    a.kind           = Action::Kind::CastFromHand;
    a.card_name      = name;
    a.def            = &RDef(name);
    a.hand_index     = -1;
    a.cost           = RDef(name).card.m_mana_cost;
    a.is_noncreature = !RDef(name).card.IsCreature();
    return a;
}
const char* kRagga = "Raggadragga, Goreguts Boss";
struct RaggadraggaChooserScope
{
    LoyaltyTargetChooser chooser;
    LoyaltyTargetChooser* saved;
    explicit RaggadraggaChooserScope(LoyaltyTargetChooser fn)
        : chooser(std::move(fn)), saved(g_play_loyalty_chooser)
    { g_play_loyalty_chooser = &chooser; }
    ~RaggadraggaChooserScope() { g_play_loyalty_chooser = saved; }
};
}   // namespace

TEST_CASE("Raggadragga: card data and the deck stamps")
{
    const CardDefinition& d = RDef(kRagga);
    CHECK(d.card.m_mana_cost.ManaValue() == 4);
    CHECK(d.card.m_power == 4);
    CHECK(d.card.m_toughness == 4);
    CHECK(d.card.HasSupertype(Supertype::Legendary));
    CHECK(d.params.lord_affects_mana_ability_creatures);
    CHECK(d.params.attack_untap_mana_ability_attackers);
    CHECK(d.params.cast_trigger_untap_pump_target_min_spent == 7);
    CHECK(d.params.cast_trigger_untap_pump_power == 7);
    CHECK(d.params.cast_trigger_untap_pump_tough == 7);
    CHECK(IsLordPermanent(d));
    CHECK(d.hand_gain_audited);   // the Herald's Horn take-fold whitelist covers its keys

    Decklist with;
    with.mainboard.push_back(d.card);
    with.mainboard.push_back(RDef("Forest").card);
    GameState a;
    GoldFishRunner::StampDeckTraits(a, with);
    CHECK(a.deck_has_attack_untap);
    CHECK(a.deck_has_cast_target_trigger);
}

TEST_CASE("Raggadragga: the anthem pumps creatures that HAVE a mana ability, sick or tapped, never itself")
{
    GameState s = RBase();
    const int birds = RPut(s, "Birds of Paradise", 10, 0, /*sick=*/true);
    const int sage  = RPut(s, "Somberwald Sage", 11);
    const int boar  = RPut(s, "Flinthoof Boar", 12);
    const int opp   = RPut(s, "Birds of Paradise", 13, /*controller=*/1);
    const int rg    = RPut(s, kRagga, 14);
    s.battlefield[sage].tapped = true;
    auto lb = [&](int i) { return ComputeLordBonus(s.battlefield[i].card, s, s.battlefield[i].controller_index,
                                                   false, &s.battlefield[i]); };
    CHECK(lb(birds) == std::make_pair(2, 2));   // summoning-sick: still has the ability
    CHECK(lb(sage) == std::make_pair(2, 2));    // tapped: still has it
    CHECK(lb(boar) == std::make_pair(0, 0));    // no mana ability
    CHECK(lb(opp) == std::make_pair(0, 0));     // not ours
    CHECK(lb(rg) == std::make_pair(0, 0));      // Raggadragga has none
    // a hand-card projection (self == nullptr) answers from the definition
    CHECK(ComputeLordBonus(RCard("Zhur-Taa Druid", 20), s, 0) == std::make_pair(2, 2));
    CHECK(ComputeLordBonus(RCard("Forest", 21), s, 0).first == 0);
}

TEST_CASE("Raggadragga: DECLARED attackers with a mana ability untap after combat damage; main 2 opens")
{
    GameState s = RBase();
    s.phase = Phase::Combat;
    const int druid = RPut(s, "Zhur-Taa Druid", 10);
    const int boar  = RPut(s, "Flinthoof Boar", 11);
    const int birds = RPut(s, "Birds of Paradise", 12);
    RPut(s, kRagga, 13);
    // the Birds is in the attack list but NOT declared (a put, CR 508.4): declared_n = 2
    const std::vector<int> atk = { druid, boar, birds };
    const std::vector<int> nums = AttackUntapManaAttackers(s, 0, atk, /*declared_n=*/2);
    REQUIRE(nums.size() == 1u);
    CHECK(nums.front() == 10);
    ResolveCombatDamage(s, atk, /*exalted_bonus=*/0, /*collect_descs=*/false);
    CHECK(RNum(s, 10).tapped);
    UntapManaAttackers(s, 0, nums);
    CHECK_FALSE(RNum(s, 10).tapped);   // the Druid untapped
    CHECK(RNum(s, 11).tapped);         // the Boar has no mana ability
    CHECK(RNum(s, 12).tapped);         // the put Birds never attacked
    CHECK(s.pay_ledger.untap_effect);
    CHECK(s.players[1].life == 20 - 3 - 2 - 2);   // the 1/1 Druid and 0/1 Birds are 3/3 and 2/3; the 2/2 Boar

    s.phase = Phase::PostCombatMain;
    CHECK(UntapSecondMainLive(s));
    for (Permanent& p : s.battlefield) { if (p.card.m_number == 10) { p.tapped = true; } }
    CHECK_FALSE(UntapSecondMainLive(s));   // no untapped mana creature: nothing to spend

    GameState none = RBase();
    const int d2 = RPut(none, "Zhur-Taa Druid", 10);
    CHECK(AttackUntapManaAttackers(none, 0, { d2 }, 1).empty());   // no Raggadragga: no trigger
    CHECK_FALSE(AttackUntapsAttacker(none, none.battlefield[d2]));
}

TEST_CASE("Raggadragga: the cast trigger reads the mana SPENT (intervening if) and untaps + pumps its target")
{
    auto board = [] {
        GameState s = RBase();
        RPut(s, kRagga, 10);
        const int sage = RPut(s, "Somberwald Sage", 11);
        s.battlefield[sage].tapped = true;
        RPut(s, "Flinthoof Boar", 12);
        return s;
    };
    const CardDefinition& er = RDef("End-Raze Forerunners");
    SUBCASE("six spent: no trigger")
    {
        GameState s = board();
        FireOnCastTriggers(s, er, 6);
        CHECK(RNum(s, 11).tapped);
        CHECK(RNum(s, 11).temp_power_bonus == 0);
        CHECK(RNum(s, 12).temp_power_bonus == 0);
        CHECK(s.cast_untap_refunds == 0);
    }
    SUBCASE("seven spent: rank 0 untaps the tapped mana creature (a refund)")
    {
        GameState s = board();
        FireOnCastTriggers(s, er, 7);
        CHECK_FALSE(RNum(s, 11).tapped);
        CHECK(RNum(s, 11).temp_power_bonus == 7);
        CHECK(RNum(s, 11).temp_tough_bonus == 7);
        CHECK(s.cast_untap_refunds == 1);
        CHECK(s.pay_ledger.untap_effect);
    }
    SUBCASE("the searched rank reaches the other classes (Boar, Raggadragga itself)")
    {
        std::vector<int> pumped;
        for (int k = 0; k < 3; ++k)
        {
            GameState s = board();
            // Boars I4c: the untap-pump rank is a per-instance tuple (UntapPumpRankPack); one digit here.
            ScriptedCastTriggerTarget pin(UntapPumpRankPack({ k }, 2), s.TurnSeq(), s.cast_untap_pump_ordinal);
            FireOnCastTriggers(s, er, 8);
            for (const Permanent& p : s.battlefield) { if (p.temp_power_bonus == 7) { pumped.push_back(p.card.m_number); } }
        }
        std::sort(pumped.begin(), pumped.end());
        CHECK(pumped == std::vector<int>{ 10, 11, 12 });
    }
    SUBCASE("human target chooser sees every legal creature after a seven-mana cast")
    {
        GameState s = board();
        int asked = 0;
        LoyaltyTargetChooser chooser = [&](const GameState& state, int controller,
                                           const std::string& source, const std::string& prompt,
                                           const std::vector<int>& legal, int) -> int
        {
            ++asked;
            CHECK(controller == 0);
            CHECK(source == kRagga);
            CHECK(prompt.find("target creature") != std::string::npos);
            CHECK(legal.size() == 3u);
            for (int i = 0; i < static_cast<int>(legal.size()); ++i)
            {
                if (state.battlefield[static_cast<std::size_t>(legal[i])].card.m_number == 12) { return i; }
            }
            FAIL("Flinthoof Boar absent from the human target choices");
            return 0;
        };
        {
            RaggadraggaChooserScope scope(std::move(chooser));
            FireOnCastTriggers(s, er, 7);
        }
        CHECK(asked == 1);
        CHECK(RNum(s, 12).temp_power_bonus == 7);
        CHECK(RNum(s, 11).temp_power_bonus == 0);
    }
    SUBCASE("the opponent's creatures are folded away in search; a shrouded creature is illegal")
    {
        GameState s = board();
        RPut(s, "Birds of Paradise", 30, /*controller=*/1);
        std::vector<std::vector<int>> classes;
        CastTriggerTargetClasses(s, 0, false, classes, /*untap_pump=*/true);
        CHECK(classes.size() == 3u);   // Raggadragga, the tapped Sage, the Boar -- no land, no opponent creature
    }
}

TEST_CASE("Raggadragga: the fold key splits tapped / untapped and sick / not")
{
    GameState s = RBase();
    RPut(s, "Birds of Paradise", 10);
    const int b2 = RPut(s, "Birds of Paradise", 11);
    RPut(s, "Birds of Paradise", 12, 0, /*sick=*/true);
    RPut(s, "Birds of Paradise", 13);
    s.battlefield[b2].tapped = true;
    std::vector<std::vector<int>> classes;
    CastTriggerTargetClasses(s, 0, false, classes, /*untap_pump=*/true);
    CHECK(classes.size() == 3u);   // {10, 13} untapped ready, {11} tapped, {12} sick
}

TEST_CASE("Raggadragga: executor and rollout realise the same trigger and the refund opens site 9")
{
    auto board = [] {
        GameState s = RBase();
        RPut(s, kRagga, 10);
        RPut(s, "Somberwald Sage", 11);
        for (int k = 0; k < 5; ++k) { RPut(s, "Forest", 50 + k); }
        s.players[0].hand.push_back(RCard("End-Raze Forerunners", 60));
        return s;
    };
    GameState roll = board();
    const std::vector<uint64_t> pre = TurnSolver::SnapshotActivatableAbilities(roll);
    TurnSolver::Plan plan;
    plan.actions = { RCast("End-Raze Forerunners") };
    TurnSolver::ApplyPlan(roll, plan, /*is_pre_combat=*/true);
    CHECK(g_cast_trigger_mana_spent_probe == 8);
    CHECK_FALSE(RNum(roll, 11).tapped);   // the Sage paid 3 and was untapped by the trigger
    CHECK(RNum(roll, 11).temp_power_bonus == 7 + 2);   // +7/+7, then End-Raze's ETB +2/+2
    CHECK(roll.cast_untap_refunds == 1);
    CHECK(TurnSolver::PostEntryActivationPending(roll, pre));   // new mana: the rest of the phase re-plans
    roll.cast_untap_refunds = 0;
    CHECK_FALSE(TurnSolver::PostEntryActivationPending(roll, pre));   // ...and it is the refund key that opened it

    GameState exec = board();
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    ManaPool avail = AvailableManaPool(exec, nullptr);
    MtgTestSeam::CastSpellFromHand(eng, exec, exec.players[0].hand.back(), avail, 0);
    CHECK(g_cast_trigger_mana_spent_probe == 8);
    CHECK_FALSE(RNum(exec, 11).tapped);
    CHECK(RNum(exec, 11).temp_power_bonus == 7);
    CHECK(exec.cast_untap_refunds == 1);
}

// Boars I4c (ledger risk 3): the target is chosen PER TRIGGER INSTANCE -- two qualifying casts in one turn take
// independent targets (the old whole-turn rank gave both the same rank).
TEST_CASE("Raggadragga I4c: two qualifying casts in one turn take independent targets")
{
    auto board = [] {
        GameState s = RBase();
        RPut(s, kRagga, 10);
        const int sage = RPut(s, "Somberwald Sage", 11);
        s.battlefield[sage].tapped = true;
        RPut(s, "Flinthoof Boar", 12);
        return s;
    };
    const CardDefinition& er = RDef("End-Raze Forerunners");
    SUBCASE("the digit codec")
    {
        CHECK(UntapPumpRankPack({ 0, 0 }, 3) == 0);
        const int k = UntapPumpRankPack({ 2, 5, 1 }, 3);
        CHECK(UntapPumpRankDigit(k, 0) == 2);
        CHECK(UntapPumpRankDigit(k, 1) == 5);
        CHECK(UntapPumpRankDigit(k, 2) == 1);
        CHECK(UntapPumpRankDigit(k, 3) == 0);
        CHECK(UntapPumpRankDigit(0, 0) == 0);
    }
    SUBCASE("every ordered pair of targets is reachable, including two DIFFERENT ones")
    {
        std::set<std::vector<int>> outcomes;
        for (int d0 = 0; d0 < 3; ++d0)
        {
            for (int d1 = 0; d1 < 3; ++d1)
            {
                GameState s = board();
                ScriptedCastTriggerTarget pin(UntapPumpRankPack({ d0, d1 }, 2), s.TurnSeq(), s.cast_untap_pump_ordinal);
                FireOnCastTriggers(s, er, 8);
                FireOnCastTriggers(s, er, 8);
                CHECK(s.cast_untap_pump_ordinal == 2);
                outcomes.insert({ RNum(s, 10).temp_power_bonus, RNum(s, 11).temp_power_bonus, RNum(s, 12).temp_power_bonus });
            }
        }
        CHECK(outcomes.count({ 0, 7, 7 }) == 1);    // untap the Sage on the first, pump the Boar on the second
        CHECK(outcomes.count({ 7, 7, 0 }) == 1);
        CHECK(outcomes.count({ 7, 0, 7 }) == 1);
        CHECK(outcomes.count({ 0, 14, 0 }) == 1);   // ...and the same target twice
        CHECK(outcomes.count({ 0, 0, 14 }) == 1);
        CHECK(outcomes.count({ 14, 0, 0 }) == 1);
        CHECK(outcomes.size() == 6u);
    }
    SUBCASE("the digit is relative to the pin's install: a later install starts at its own digit 0")
    {
        GameState s = board();
        FireOnCastTriggers(s, er, 8);   // no pin: rank 0 -> the tapped Sage
        CHECK(RNum(s, 11).temp_power_bonus == 7);
        // A re-solve installs (2nd digit = Raggadragga) at the current ordinal: its digit 0 is the NEXT trigger.
        ScriptedCastTriggerTarget pin(UntapPumpRankPack({ 0, 1 }, 2), s.TurnSeq(), s.cast_untap_pump_ordinal);
        CHECK(UntapPumpRankFor(s) == 0);
        CHECK(UntapPumpRankFor(s, 1) == 1);
    }
}

// The SEARCH finds a line that needs the two instances on DIFFERENT targets. Two tapped Apex Altisaurs (10/10), a
// summoning-sick Raggadragga, two Elderscale Wurms in hand and 14 Forests; the opponent at 30. Untapping both Altisaurs
// (+7/+7 each) attacks for 34 -- lethal. Any single target for both triggers untaps one Altisaur at most (17): under the
// old shared rank, rank 0 names the Altisaur class on the first trigger and the untapped Altisaur (now first, an
// attacker) on the second, so the kill was unfindable at any budget.
TEST_CASE("Raggadragga I4c: the search untaps two different creatures with two qualifying casts")
{
    auto board = [] {
        GameState s = RBase();
        s.players[1].life = 30;
        RPut(s, kRagga, 10, 0, /*sick=*/true);
        const int a1 = RPut(s, "Apex Altisaur", 11);
        const int a2 = RPut(s, "Apex Altisaur", 12);
        s.battlefield[a1].tapped = true;
        s.battlefield[a2].tapped = true;
        for (int k = 0; k < 14; ++k) { RPut(s, "Forest", 50 + k); }
        s.players[0].hand.push_back(RCard("Elderscale Wurm", 60));
        s.players[0].hand.push_back(RCard("Elderscale Wurm", 61));
        return s;
    };
    {
        GameState s = board();
        const TurnSolver::Plan pick = TurnSolver::SolveWithLookahead(s, /*is_pre_combat=*/true, /*depth=*/2);
        TurnSolver::ApplyPlan(s, pick, /*is_pre_combat=*/true);
        CHECK(s.cast_untap_pump_ordinal == 2);
        CHECK_FALSE(RNum(s, 11).tapped);
        CHECK_FALSE(RNum(s, 12).tapped);
        CHECK(RNum(s, 11).temp_power_bonus == 7);
        CHECK(RNum(s, 12).temp_power_bonus == 7);
    }
    {
        GameState s = board();
        AIEngine eng(MulliganProfile::DefaultProfile(), 2, 20);
        eng.TakeTurn(s, /*is_pre_combat_main=*/true, [](GameState& st) {
            while (!st.stack.empty())
            {
                const StackEntry e = st.stack.back();
                st.stack.pop_back();
                const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
                if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
            }
        });
        CHECK_FALSE(RNum(s, 11).tapped);
        CHECK_FALSE(RNum(s, 12).tapped);
        CHECK(RNum(s, 11).temp_power_bonus == 7);
        CHECK(RNum(s, 12).temp_power_bonus == 7);
    }
}
