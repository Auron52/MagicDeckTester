// Unit tests for the Karoo drop's SEARCHED POSITION (MTG_KAROO_POSITION -> KarooPositionSearchOn,
// heurarm slot KAROO_POSITION; Plan::land_slot <= -2), 2026-10-22. USER: "We should allow the sanctuary
// to be played at the end of the phase at latest".
//
// A Karoo (Selesnya Sanctuary: enters tapped, "return a land you control to its owner's hand") is
// played AFTER the plan's casts by default, so its mandatory bounce returns a land the casts already
// spent. That stays the default and is always enumerated; what was missing is every EARLIER point:
//  1. "Sanctuary, then Rest for the Weary" gains 8 BEFORE combat (a land entered when Rest resolves),
//     in BOTH worlds (ApplyPlanDirect's karoo_pre_hook, AIEngine::TakeTurn's exec_karoo_pre_hook).
//  2. The after-the-casts line still exists and still gains 4 (Rest resolves before the drop).
//  3. "Tap the land for mana, play the Sanctuary returning it, cast with the floating mana": with two
//     untapped lands and Rest's {1}{W}, playing the Sanctuary first bounces an UNTAPPED land, whose mana
//     is floated in response (MTG_BOUNCE_UNTAPPED_FIRST), and that float pays Rest.
//  4. The ENUMERATION offers the earlier position only where it can matter (the provider prune,
//     DecisionProvider::KarooEntryObserver): before a reader of the land-entry condition, before every
//     cast in a deck with a landfall trigger, nowhere in a deck with neither -- and nothing with the
//     lever off.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/TurnSolver.h"
#include "ai/DecisionProviders.h"
#include "ai/EngineFlags.h"
#include "ai/HeuristicArm.h"
#include "ai/LandPlay.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsKp()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(
            ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefKp(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

struct ArmKp
{
    int slot; std::int8_t prev;
    ArmKp(int sl, bool on) : slot(sl), prev(heurarm::t_arm[sl]) { heurarm::t_arm[sl] = on ? 1 : 0; }
    ~ArmKp() { heurarm::t_arm[slot] = prev; }
};

GameState FreshKp()
{
    EnsureCardsKp();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.deck_has_landfall   = false;   // SelesnyaLifegain's stamp: no landfall TRIGGER in the deck
    for (int k = 0; k < 20; ++k)
    {
        Card c = DefKp("Forest").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

void PutKp(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = DefKp(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

void HandKp(GameState& s, const std::string& name, int number)
{
    Card c = DefKp(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

Action CastKp(const GameState& s, const std::string& name, int number)
{
    const CardDefinition& d = DefKp(name);
    Action a;
    a.kind           = Action::Kind::CastFromHand;
    a.card_name      = d.card.m_name;
    a.def            = &d;
    a.cost           = d.card.m_mana_cost;
    a.card_mv        = d.card.m_mana_cost.ManaValue();
    a.is_noncreature = !d.card.IsCreature();
    a.hand_index     = -1;
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i)
    { if (s.players[0].hand[static_cast<std::size_t>(i)].m_number == number) { a.hand_index = i; } }
    return a;
}

void ResolveAllKp(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

GameState RollKp(const GameState& s, const TurnSolver::Plan& p)
{
    GameState t = s;
    TurnSolver::ApplyPlan(t, p, /*is_pre_combat=*/true);
    return t;
}

GameState ExecKp(const GameState& s, const TurnSolver::Plan& p)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, p, true);
    GameState t = s;
    eng.TakeTurn(t, true, ResolveAllKp);
    return t;
}

// T4: Plains + Forest untapped (exactly Rest's {1}{W}), Sanctuary and Rest for the Weary in hand.
GameState SanctuaryRestBoard()
{
    GameState s = FreshKp();
    PutKp(s, "Plains", 1);
    PutKp(s, "Forest", 2);
    HandKp(s, "Selesnya Sanctuary", 40);
    HandKp(s, "Rest for the Weary", 50);
    return s;
}

TurnSolver::Plan SanctuaryPlan(const GameState& s, int8_t land_slot)
{
    TurnSolver::Plan p;
    p.actions.push_back(CastKp(s, "Rest for the Weary", 50));
    p.land_decided = true;
    p.land_to_play = "Selesnya Sanctuary";
    p.land_slot    = land_slot;
    return p;
}

bool OnBattlefieldKp(const GameState& s, const std::string& name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { return true; } }
    return false;
}

bool InZoneKp(const std::vector<Card>& z, const std::string& name)
{
    for (const Card& c : z) { if (c.m_name.str() == name) { return true; } }
    return false;
}

int BasicsInHand(const GameState& s)
{
    int n = 0;
    for (const Card& c : s.players[0].hand)
    { if (c.m_name.str() == "Plains" || c.m_name.str() == "Forest") { ++n; } }
    return n;
}

// Both worlds, one plan: Rest resolved for `gain`, the Sanctuary is on the battlefield, and exactly one
// basic went back to hand.
void CheckKp(const GameState& s, const TurnSolver::Plan& p, int gain)
{
    const int life0 = s.players[0].life;
    for (int world = 0; world < 2; ++world)
    {
        CAPTURE(world);   // 0 = rollout (ApplyPlan), 1 = executor (AIEngine::TakeTurn + EffectHandler)
        const GameState t = world == 0 ? RollKp(s, p) : ExecKp(s, p);
        REQUIRE(InZoneKp(t.players[0].graveyard, "Rest for the Weary"));   // cast and resolved
        CHECK(t.players[0].life == life0 + gain);
        CHECK(t.players[0].life_gained_this_turn == gain);
        CHECK(OnBattlefieldKp(t, "Selesnya Sanctuary"));
        CHECK(t.players[0].lands_played_this_turn == 1);
        CHECK(BasicsInHand(t) == 1);                                       // the mandatory bounce
        CHECK_FALSE(InZoneKp(t.players[0].hand, "Selesnya Sanctuary"));
    }
}

int CountSanctuaryPlans(const std::vector<TurnSolver::Plan>& plans, const std::string& cast, int8_t slot)
{
    int n = 0;
    for (const TurnSolver::Plan& p : plans)
    {
        if (!p.land_decided || p.land_to_play != "Selesnya Sanctuary" || p.land_slot != slot) { continue; }
        for (const Action& a : p.actions)
        { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == cast) { ++n; break; } }
    }
    return n;
}

}   // namespace

TEST_CASE("Karoo position: the encoding round-trips and never collides with the land-slot values")
{
    CHECK(TurnSolver::KarooSlotBefore(1) == -2);
    CHECK(TurnSolver::KarooSlotOrdinal(TurnSolver::KarooSlotBefore(1)) == 1);
    CHECK(TurnSolver::KarooSlotOrdinal(TurnSolver::KarooSlotBefore(TurnSolver::kKarooSlotMax))
          == TurnSolver::kKarooSlotMax);
    CHECK_FALSE(TurnSolver::KarooSlotEvery(TurnSolver::KarooSlotBefore(1)));
    CHECK(TurnSolver::KarooSlotEvery(TurnSolver::KarooSlotBefore(1, true)));
    CHECK(TurnSolver::KarooSlotOrdinal(TurnSolver::KarooSlotBefore(1, true)) == 1);
    CHECK(TurnSolver::KarooSlotOrdinal(TurnSolver::KarooSlotBefore(TurnSolver::kKarooSlotMax, true))
          == TurnSolver::kKarooSlotMax);
    CHECK(TurnSolver::KarooSlotBefore(TurnSolver::kKarooSlotMax) > TurnSolver::KarooSlotBefore(1, true));
    CHECK(TurnSolver::KarooSlotOrdinal(0) == 0);
    CHECK(TurnSolver::KarooSlotOrdinal(TurnSolver::kLandSlotAfterCasts) == 0);
    CHECK(TurnSolver::KarooSlotOrdinal(3) == 0);
}

TEST_CASE("Karoo position: Sanctuary BEFORE Rest for the Weary gains 8 pre-combat, in both worlds")
{
    const GameState s = SanctuaryRestBoard();
    CheckKp(s, SanctuaryPlan(s, TurnSolver::KarooSlotBefore(1)), 8);
}

TEST_CASE("Karoo position: the after-the-casts default is unchanged -- Rest first gains 4, then the drop")
{
    const GameState s = SanctuaryRestBoard();
    CheckKp(s, SanctuaryPlan(s, 0), 4);
}

TEST_CASE("Karoo position: an unreached position falls back to after the casts (never lost)")
{
    const GameState s = SanctuaryRestBoard();
    // Position 2 names a second counted cast the plan does not have: the drop still happens, at the end.
    CheckKp(s, SanctuaryPlan(s, TurnSolver::KarooSlotBefore(2)), 4);
}

TEST_CASE("Karoo position: tap the land, play the Sanctuary returning it, cast with the FLOATING mana")
{
    // Two untapped basics are exactly Rest's {1}{W}, and a one-cast plan is never batch-prepaid, so
    // the Sanctuary played first bounces an UNTAPPED land: it is tapped for mana in response
    // (KarooTapInResponseOn), and that floating mana is what pays Rest together with the other land.
    REQUIRE(KarooTapInResponseOn());
    GameState s = SanctuaryRestBoard();
    {
        GameState t = s;
        REQUIRE(t.players[0].hand[0].m_name.str() == "Selesnya Sanctuary");
        REQUIRE(PlayLandFromHand(t, 0, DefKp("Selesnya Sanctuary"), LandPlayOptions{}));
        CHECK(BasicsInHand(t) == 1);
        CHECK(t.floating_mana.Total() == 1);          // the returned basic's mana, floated
        int untapped_basics = 0;
        for (const Permanent& p : t.battlefield)
        {
            if ((p.card.m_name.str() == "Plains" || p.card.m_name.str() == "Forest") && !p.tapped)
            { ++untapped_basics; }
        }
        CHECK(untapped_basics == 1);                  // the other basic, still untapped
    }
    // ...and the whole line, in both worlds: 8 life, nothing left floating, both basics' mana used.
    const TurnSolver::Plan p = SanctuaryPlan(s, TurnSolver::KarooSlotBefore(1));
    for (int world = 0; world < 2; ++world)
    {
        CAPTURE(world);
        const GameState t = world == 0 ? RollKp(s, p) : ExecKp(s, p);
        CHECK(t.players[0].life == s.players[0].life + 8);
        CHECK(t.floating_mana.Total() == 0);
    }
}

TEST_CASE("Karoo position: the enumeration offers the earlier point only where an observer can see it")
{
    SUBCASE("a land-entry READER (Rest for the Weary): both positions are enumerated")
    {
        ArmKp on(heurarm::KAROO_POSITION, true);
        const GameState s = SanctuaryRestBoard();
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        CHECK(CountSanctuaryPlans(plans, "Rest for the Weary", 0) >= 1);
        CHECK(CountSanctuaryPlans(plans, "Rest for the Weary", TurnSolver::KarooSlotBefore(1)) >= 1);
        CHECK(CountSanctuaryPlans(plans, "Rest for the Weary", TurnSolver::KarooSlotBefore(2)) == 0);
    }
    SUBCASE("the lever OFF: the after-the-casts plan only")
    {
        ArmKp off(heurarm::KAROO_POSITION, false);
        const GameState s = SanctuaryRestBoard();
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        CHECK(CountSanctuaryPlans(plans, "Rest for the Weary", 0) >= 1);
        CHECK(CountSanctuaryPlans(plans, "Rest for the Weary", TurnSolver::KarooSlotBefore(1)) == 0);
    }
    SUBCASE("no observer (Heroes' Reunion, no landfall trigger in the deck): pruned -- no earlier point")
    {
        ArmKp on(heurarm::KAROO_POSITION, true);
        GameState s = FreshKp();
        PutKp(s, "Plains", 1);
        PutKp(s, "Forest", 2);
        HandKp(s, "Selesnya Sanctuary", 40);
        HandKp(s, "Heroes' Reunion", 50);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        CHECK(CountSanctuaryPlans(plans, "Heroes' Reunion", 0) >= 1);
        CHECK(CountSanctuaryPlans(plans, "Heroes' Reunion", TurnSolver::KarooSlotBefore(1)) == 0);
        CHECK_FALSE(GenericProvider().KarooEntryObserver(s, DefKp("Heroes' Reunion")));
        CHECK(GenericProvider().KarooEntryObserver(s, DefKp("Rest for the Weary")));
    }
    SUBCASE("a deck with a landfall TRIGGER: every cast is an observer (its payload may feed it)")
    {
        ArmKp on(heurarm::KAROO_POSITION, true);
        GameState s = FreshKp();
        s.deck_has_landfall = true;
        PutKp(s, "Plains", 1);
        PutKp(s, "Forest", 2);
        HandKp(s, "Selesnya Sanctuary", 40);
        HandKp(s, "Heroes' Reunion", 50);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        CHECK(CountSanctuaryPlans(plans, "Heroes' Reunion", TurnSolver::KarooSlotBefore(1)) >= 1);
        CHECK(GenericProvider().KarooEntryObserver(s, DefKp("Heroes' Reunion")));
    }
}
