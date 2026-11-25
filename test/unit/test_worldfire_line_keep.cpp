// Worldfire APPLY-DROP (mana-clean gate claude-queue-20261125a, 6,938 of the deck's 7,392 issues): two reach gaps of
// the crit-mode line-keep rung (LineKeepCritGate / LineKeepBound; docs/design/worldfire-apply-drop-2026-11-25.md).
//   MTG_LINE_KEEP_SUSPEND    -- a {suspend + one cast} line was unbound: a paid suspend was not a held payment.
//   MTG_LINE_KEEP_CRIT_MULTI -- the gate saw only ONE source as the last of a colour, not several together.
// Both boards are the gate's own (Mercadian Bazaar, which taps last as a storage land, beside the deck's blue sources).
#include <doctest/doctest.h>

#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{
void EnsureCardsWfl()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefWfl(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Permanent& PutWfl(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = DefWfl(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}

Card CardWfl(const std::string& name, int number)
{
    Card c     = DefWfl(name).card;
    c.m_number = number;
    return c;
}

struct ArmWfl
{
    heurarm::Slot slot;
    ArmWfl(heurarm::Slot s, bool on) : slot(s) { heurarm::t_arm[s] = on ? 1 : 0; }
    ~ArmWfl() { heurarm::t_arm[slot] = -1; }
};

Action CastWfl(const GameState& s, const std::string& n)
{
    Action a;
    a.kind      = Action::Kind::CastFromHand;
    a.card_name = n;
    a.def       = &DefWfl(n);
    a.cost      = EffectiveSpellCost(DefWfl(n), s);
    return a;
}

// The enumerator's suspend action (TurnSolver's Suspend emission): cost = the card's suspend cost.
Action SuspendWfl(const std::string& n)
{
    Action a;
    a.kind           = Action::Kind::Suspend;
    a.card_name      = n;
    a.def            = &DefWfl(n);
    a.cost           = DefWfl(n).params.suspend_cost.value_or(ManaCost{});
    a.is_noncreature = true;
    return a;
}

GameState BaseWfl(int turn)
{
    EnsureCardsWfl();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(CardWfl(k % 2 ? "Island" : "Mountain", 900 + k)); }
    return s;
}

// Pay line[0] under the seeds the apply worlds bind (LineUnpaidCostScope + LineReducerCreditScope, the AIEngine /
// ApplyPlanDirect twin), then whether line[1]'s cost is still payable.
bool PayFirstThenRestWfl(GameState& s, const std::vector<Action>& line)
{
    LineUnpaidCostScope luc(LineCastCostTotal(line));
    const int credit = LineReducerCredit(s, line);
    const int bound = LineKeepBound(line);
    LineReducerCreditScope lrc(credit, (credit > 0 || bound) ? LineNoncreatureMv(line) : 0, bound);
    REQUIRE(TapForCostDirect(s, line[0].cost, /*for_creature=*/false));
    SubManaCost(g_line_unpaid_cost, line[0].cost);   // the cast / ApplySuspend site's decrement
    return TapForCostDirect(s, line[1].cost, /*for_creature=*/false);
}
}   // namespace

TEST_CASE("Worldfire line keep: a paid suspend is a held payment of the line (MTG_LINE_KEEP_SUSPEND)")
{
    GameState s0 = BaseWfl(3);
    const std::vector<Action> line = { SuspendWfl("Keldon Halberdier"), CastWfl(s0, "Ponder") };
    const std::vector<Action> bloom = { SuspendWfl("Lotus Bloom"), CastWfl(s0, "Ponder") };
    ArmWfl crit(heurarm::LINE_KEEP_CRIT, true);
    ArmWfl all(heurarm::LINE_KEEP_ALL, false);
    {
        ArmWfl su(heurarm::LINE_KEEP_SUSPEND, true);
        CHECK(LineKeepBound(line) == kLineKeepCrit);
        CHECK(LineKeepBound(bloom) == 0);                  // a {0} suspend pays nothing: not a payment
        CHECK(LineNoncreatureMv(line) == 2);               // Halberdier's {R} is no creature spell's cost
    }
    {
        ArmWfl su(heurarm::LINE_KEEP_SUSPEND, false);
        CHECK(LineKeepBound(line) == 0);
        CHECK(LineNoncreatureMv(line) == 1);
    }
}

// Worldfire s30000447 gi447 T3 m1 {suspend Keldon Halberdier + Ponder}: Mercadian Bazaar (one storage counter) + Steam
// Vents. The greedy pays the {R} with Vents (a storage land taps last) and Ponder's {U} has no source. With the suspend
// bound, the rung's trial sees the strand and holds Vents out: the Bazaar pays the {R}, Vents the {U}.
TEST_CASE("Worldfire line keep: s30000447 T3 -- the Bazaar pays Halberdier's suspend and Steam Vents stays for Ponder")
{
    GameState s0 = BaseWfl(3);
    PutWfl(s0, "Mercadian Bazaar", 20).storage_counters = 1;
    PutWfl(s0, "Steam Vents", 54);
    const std::vector<Action> line = { SuspendWfl("Keldon Halberdier"), CastWfl(s0, "Ponder") };
    ArmWfl crit(heurarm::LINE_KEEP_CRIT, true);
    ArmWfl all(heurarm::LINE_KEEP_ALL, false);
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmWfl su(heurarm::LINE_KEEP_SUSPEND, on);
        GameState s = s0;
        CHECK(PayFirstThenRestWfl(s, line) == on);
    }
    // A second blue source: nothing strands, both arms tap the same lands (the Bazaar keeps its counter).
    {
        GameState a = s0;
        GameState b = s0;
        PutWfl(a, "Island", 9);
        PutWfl(b, "Island", 9);
        {
            ArmWfl su(heurarm::LINE_KEEP_SUSPEND, true);
            CHECK(PayFirstThenRestWfl(a, line));
        }
        {
            ArmWfl su(heurarm::LINE_KEEP_SUSPEND, false);
            CHECK(PayFirstThenRestWfl(b, line));
        }
        for (std::size_t i = 0; i < a.battlefield.size(); ++i)
        {
            CAPTURE(i);
            CHECK(a.battlefield[i].tapped == b.battlefield[i].tapped);
            CHECK(a.battlefield[i].storage_counters == b.battlefield[i].storage_counters);
        }
        CHECK(a.battlefield[0].storage_counters == 1);
    }
}

// Worldfire s30000065 gi65 T4 m1 {Seething Song + Brainstorm}: Steam Vents, Mercadian Bazaar (one counter), Saprazzan
// Skerry (two depletion counters, {U}{U} a tap). The greedy pays Song's {2}{R} with Vents' {R} + Skerry's {U}{U}, the
// Bazaar held, and Brainstorm's {U} has no source. Neither Vents (1) nor Skerry (2) is alone the last {U} of three, so
// the one-source gate never asked the judge; the multi-source gate does, and the Bazaar's {R} + Skerry pay Song.
TEST_CASE("Worldfire line keep: s30000065 T4 -- Song is paid by the Bazaar + Skerry, Steam Vents stays for Brainstorm")
{
    GameState s0 = BaseWfl(4);
    PutWfl(s0, "Steam Vents", 54);
    PutWfl(s0, "Mercadian Bazaar", 20).storage_counters = 1;
    PutWfl(s0, "Saprazzan Skerry", 42).counters.Add(Counter::Type::Depletion, 2);
    const std::vector<Action> line = { CastWfl(s0, "Seething Song"), CastWfl(s0, "Brainstorm") };
    ArmWfl crit(heurarm::LINE_KEEP_CRIT, true);
    ArmWfl all(heurarm::LINE_KEEP_ALL, false);
    REQUIRE(LineKeepBound(line) == kLineKeepCrit);
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmWfl mu(heurarm::LINE_KEEP_CRIT_MULTI, on);
        GameState s = s0;
        CHECK(PayFirstThenRestWfl(s, line) == on);
    }
    // With an Island up there is a {U} to spare: the gate may pass, the judge finds no strand, both arms pay alike.
    {
        GameState a = s0;
        GameState b = s0;
        PutWfl(a, "Island", 9);
        PutWfl(b, "Island", 9);
        {
            ArmWfl mu(heurarm::LINE_KEEP_CRIT_MULTI, true);
            CHECK(PayFirstThenRestWfl(a, line));
        }
        {
            ArmWfl mu(heurarm::LINE_KEEP_CRIT_MULTI, false);
            CHECK(PayFirstThenRestWfl(b, line));
        }
        for (std::size_t i = 0; i < a.battlefield.size(); ++i)
        {
            CAPTURE(i);
            CHECK(a.battlefield[i].tapped == b.battlefield[i].tapped);
        }
    }
}

// Worldfire s30000125 gi125 T4 m1 {Seething Song + Seething Song + Personal Tutor}: Steam Vents, Mercadian Bazaar (one
// counter), Saprazzan Skerry. The second Song is paid out of the first's {R}x5, so the rest after the first Song's
// payment ({2}{R} + {U}) is payable only with that float: judged without it (the rung before MTG_LINE_KEEP_RITUAL) the
// joint read as 7 mana on a 4-mana board and nothing was repaired, so the greedy's Vents + Skerry left Personal Tutor
// with no {U}. With the float credited the rung holds Vents out: the Bazaar + Skerry pay the first Song.
TEST_CASE("Worldfire line keep: s30000125 T4 -- a ritual's float is on the board the rest is judged on")
{
    GameState s0 = BaseWfl(4);
    PutWfl(s0, "Steam Vents", 54);
    PutWfl(s0, "Mercadian Bazaar", 20).storage_counters = 1;
    PutWfl(s0, "Saprazzan Skerry", 42).counters.Add(Counter::Type::Depletion, 2);
    const std::vector<Action> line = { CastWfl(s0, "Seething Song"), CastWfl(s0, "Seething Song"),
                                       CastWfl(s0, "Personal Tutor") };
    const Card song = CardWfl("Seething Song", 30);
    ArmWfl crit(heurarm::LINE_KEEP_CRIT, true);
    ArmWfl all(heurarm::LINE_KEEP_ALL, false);
    ArmWfl mu(heurarm::LINE_KEEP_CRIT_MULTI, true);
    REQUIRE(LineKeepBound(line) == kLineKeepCrit);
    for (const bool on : { true, false })
    {
        CAPTURE(on);
        ArmWfl ri(heurarm::LINE_KEEP_RITUAL, on);
        GameState s = s0;
        LineUnpaidCostScope luc(LineCastCostTotal(line));
        LineReducerCreditScope lrc(0, LineNoncreatureMv(line), LineKeepBound(line));
        {
            SpellSubtypePayScope pay(&song);
            REQUIRE(TapForCostDirect(s, line[0].cost, /*for_creature=*/false));
        }
        SubManaCost(g_line_unpaid_cost, line[0].cost);
        s.floating_mana.red += 5;   // the first Song resolves
        {
            SpellSubtypePayScope pay(&song);
            REQUIRE(TapForCostDirect(s, line[1].cost, /*for_creature=*/false));
        }
        SubManaCost(g_line_unpaid_cost, line[1].cost);
        s.floating_mana.red += 5;   // the second Song resolves
        CHECK(TapForCostDirect(s, line[2].cost, /*for_creature=*/false) == on);
    }
}
