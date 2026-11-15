// Boars I3a (2026-11-13): End-Raze Forerunners -- "Vigilance, trample, haste / When this creature enters, other
// creatures you control get +2/+2 and gain vigilance and trample until end of turn."
// (etb_other_creatures_pump_power / _tough / etb_other_creatures_grant_vigilance, in the shared
// FireOwnEtbTriggersBody beside Craterhoof), the NEW Permanent::temp_vigilance (read by CreatureHasVigilance at the
// attack-tap sites; cleared at both cleanup sites; a boundary assertion in Dominance), and the per-turn
// combat-kept-mana second main (UntapSecondMainLive's second clause, Boars D6). Trample is inert (D2).
#include <doctest/doctest.h>
#include "ai/AIEngine.h"
#include "ai/Combat.h"
#include "ai/Dominance.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "core/EffectHandler.h"
#include "runner/GoldFishRunner.h"
#include "mtg_test_seam.h"
#include <string>
#include <vector>

namespace
{
const CardDefinition& EDef(const char* name)
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
Card ECard(const char* name, int number)
{
    Card c = EDef(name).card;
    c.m_number = number;
    return c;
}
int EPut(GameState& s, const char* name, int number, int controller = 0, bool sick = false)
{
    Permanent p;
    p.card = ECard(name, number);
    p.controller_index = p.owner_index = controller;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}
GameState EBase()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(ECard("Forest", 900 + k)); }
    return s;
}
const Permanent& ENum(const GameState& s, int num)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == num) { return p; } }
    FAIL("no permanent " << num);
    return s.battlefield.front();
}
Action ECast(const char* name)
{
    Action a;
    a.kind           = Action::Kind::CastFromHand;
    a.card_name      = name;
    a.def            = &EDef(name);
    a.hand_index     = -1;
    a.cost           = EDef(name).card.m_mana_cost;
    a.is_noncreature = !EDef(name).card.IsCreature();
    return a;
}
}   // namespace

TEST_CASE("End-Raze Forerunners: card data")
{
    const CardDefinition& d = EDef("End-Raze Forerunners");
    CHECK(d.params.etb_other_creatures_pump_power == 2);
    CHECK(d.params.etb_other_creatures_pump_tough == 2);
    CHECK(d.params.etb_other_creatures_grant_vigilance);
    CHECK(d.card.m_mana_cost.ManaValue() == 8);
    CHECK(d.card.m_power == 7);
    CHECK(d.card.m_toughness == 7);
    CHECK(d.card.HasKeyword(Keyword::Vigilance));
    CHECK(d.card.HasKeyword(Keyword::Haste));
    CHECK(d.card.HasKeyword(Keyword::Trample));
    CHECK(d.hand_gain_audited);   // the Herald's Horn take-fold whitelist covers its keys
}

TEST_CASE("End-Raze Forerunners: the ETB pumps every OTHER own creature +2/+2 and grants vigilance, at resolution")
{
    GameState s = EBase();
    EPut(s, "Zhur-Taa Druid", 10);
    EPut(s, "Birds of Paradise", 11, 0, /*sick=*/true);   // sick creatures are pumped too
    EPut(s, "Forest", 12);                                 // a land: untouched
    EPut(s, "Flinthoof Boar", 13, /*controller=*/1);       // the opponent's: untouched
    const int er = EPut(s, "End-Raze Forerunners", 14, 0, true);
    FireOwnEtbTriggers(s, 0, er, std::string(), 0);

    for (int n : { 10, 11 })
    {
        CAPTURE(n);
        CHECK(ENum(s, n).temp_power_bonus == 2);
        CHECK(ENum(s, n).temp_tough_bonus == 2);
        CHECK(ENum(s, n).temp_vigilance);
        CHECK(CreatureHasVigilance(ENum(s, n), s));
    }
    CHECK(ENum(s, 12).temp_power_bonus == 0);
    CHECK_FALSE(ENum(s, 12).temp_vigilance);
    CHECK(ENum(s, 13).temp_power_bonus == 0);
    CHECK_FALSE(ENum(s, 13).temp_vigilance);
    // "OTHER": End-Raze itself is not pumped; its own vigilance is the printed keyword.
    CHECK(ENum(s, 14).temp_power_bonus == 0);
    CHECK_FALSE(ENum(s, 14).temp_vigilance);
    CHECK(CreatureHasVigilance(ENum(s, 14), s));

    // A creature entering LATER gets nothing (CR 611.2c); a second End-Raze stacks +2/+2 on the first.
    const int late = EPut(s, "Ignoble Hierarch", 15);
    CHECK(s.battlefield[late].temp_power_bonus == 0);
    CHECK_FALSE(s.battlefield[late].temp_vigilance);
    const int er2 = EPut(s, "End-Raze Forerunners", 16, 0, true);
    FireOwnEtbTriggers(s, 0, er2, std::string(), 0);
    CHECK(ENum(s, 10).temp_power_bonus == 4);
    CHECK(ENum(s, 14).temp_power_bonus == 2);
    CHECK(ENum(s, 16).temp_power_bonus == 0);
}

TEST_CASE("End-Raze Forerunners: executor and rollout realise the same ETB from a real hand cast")
{
    auto board = [] {
        GameState s = EBase();
        EPut(s, "Zhur-Taa Druid", 10);
        for (int k = 0; k < 8; ++k) { EPut(s, "Forest", 50 + k); }
        s.players[0].hand.push_back(ECard("End-Raze Forerunners", 60));
        return s;
    };
    GameState roll = board();
    TurnSolver::Plan plan;
    plan.actions = { ECast("End-Raze Forerunners") };
    TurnSolver::ApplyPlan(roll, plan, /*is_pre_combat=*/true);
    CHECK(ENum(roll, 10).temp_power_bonus == 2);
    CHECK(ENum(roll, 10).temp_vigilance);
    CHECK_FALSE(ENum(roll, 10).tapped);   // eight Forests paid; the Druid was not needed
    CHECK(ENum(roll, 60).temp_power_bonus == 0);

    GameState exec = board();
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    ManaPool avail = AvailableManaPool(exec);
    MtgTestSeam::CastSpellFromHand(eng, exec, exec.players[0].hand.back(), avail, 0);
    REQUIRE(exec.stack.size() == 1u);   // the executor pushes the spell; resolve it as the engine would
    const StackEntry e = exec.stack.back();
    exec.stack.pop_back();
    EffectHandler::Resolve(exec, e, *CardDatabase::Instance().LookupCached(e.source));
    CHECK(ENum(exec, 10).temp_power_bonus == 2);
    CHECK(ENum(exec, 10).temp_vigilance);
    CHECK(ENum(exec, 60).temp_power_bonus == 0);
}

TEST_CASE("temp_vigilance: a granted attacker stays untapped through combat damage; an ungranted one taps")
{
    GameState s = EBase();
    s.phase = Phase::Combat;
    const int druid = EPut(s, "Zhur-Taa Druid", 10);
    const int boar  = EPut(s, "Flinthoof Boar", 11);
    const int er    = EPut(s, "End-Raze Forerunners", 12);
    s.battlefield[druid].temp_vigilance = true;
    s.battlefield[druid].temp_power_bonus = 2;
    ResolveCombatDamage(s, { druid, boar, er }, /*exalted_bonus=*/0, /*collect_descs=*/false);
    CHECK_FALSE(ENum(s, 10).tapped);   // granted
    CHECK(ENum(s, 11).tapped);         // not granted
    CHECK_FALSE(ENum(s, 12).tapped);   // printed vigilance
    CHECK(s.players[1].life < 20 - 3 - 7);   // the pumped Druid (3) and End-Raze (7) connected, plus the Boar
}

TEST_CASE("temp_vigilance: refused at a clean boundary, and the rollout's cleanup clears it")
{
    GameState s = EBase();
    const int d = EPut(s, "Zhur-Taa Druid", 10);
    CHECK(dominance::AtCleanBoundary(s));
    s.battlefield[d].temp_vigilance = true;
    CHECK_FALSE(dominance::AtCleanBoundary(s));
    CHECK(CreatureHasVigilance(s.battlefield[d], s));

    REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(s));
    CHECK_FALSE(ENum(s, 10).temp_vigilance);
    CHECK_FALSE(CreatureHasVigilance(ENum(s, 10), s));
}

TEST_CASE("Combat-kept mana opens this turn's second main (UntapSecondMainLive, Boars D6)")
{
    SUBCASE("the deck stamp: End-Raze in the mainboard arms it, its absence does not")
    {
        Decklist with;
        with.mainboard.push_back(EDef("End-Raze Forerunners").card);
        with.mainboard.push_back(EDef("Forest").card);
        GameState a;
        GoldFishRunner::StampDeckTraits(a, with);
        CHECK(a.deck_has_attack_untap);
        Decklist without;
        without.mainboard.push_back(EDef("Zhur-Taa Druid").card);
        without.mainboard.push_back(EDef("Forest").card);
        GameState b;
        GoldFishRunner::StampDeckTraits(b, without);
        CHECK_FALSE(b.deck_has_attack_untap);
    }
    GameState s = EBase();
    s.phase = Phase::PostCombatMain;
    s.deck_has_attack_untap = true;
    const int druid = EPut(s, "Zhur-Taa Druid", 10);
    const int boar  = EPut(s, "Flinthoof Boar", 11);
    CHECK_FALSE(UntapSecondMainLive(s));                 // no grant: nothing kept
    s.battlefield[boar].temp_vigilance = true;
    CHECK_FALSE(UntapSecondMainLive(s));                 // a granted creature with NO mana ability
    s.battlefield[druid].temp_vigilance = true;
    CHECK(UntapSecondMainLive(s));                       // a granted, untapped mana creature
    s.battlefield[druid].tapped = true;
    CHECK_FALSE(UntapSecondMainLive(s));                 // tapped (e.g. paid for End-Raze): nothing kept
    s.battlefield[druid].tapped = false;
    s.deck_has_attack_untap = false;
    CHECK_FALSE(UntapSecondMainLive(s));                 // the deck constant short-circuits
    s.deck_has_attack_untap = true;
    s.battlefield[druid].controller_index = 1;
    CHECK_FALSE(UntapSecondMainLive(s));                 // not ours
}
