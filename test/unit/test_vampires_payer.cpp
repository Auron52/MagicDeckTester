// Unit tests for the Vampires payer defects F and L (claude-play sweep 1, 2026-10-18):
//
//   F  Voldaren Estate's "{T}, Pay 1 life: Add one mana of any color" was taken while a PAINLESS payment
//      existed. In the scarcity greedy it ties Secluded Courtyard / Unclaimed Territory at the clamp-59
//      rank, so battlefield order decided; in the backtracker (the per-cast fallback and the whole-turn
//      prepay's joint solve) the first assignment its DFS reaches decided. Life lost, and the Estate's
//      {T} -- which its Blood activation wants -- spent. MTG_PAY_PAIN_LAST.
//   L  Falkenrath Pit Fighter's {R} (a creature SPELL) took the Courtyard while a Territory could pay it
//      -- both rank 59 -- and the plan's post-combat "{1}{R}, discard, sacrifice a Vampire: draw two"
//      (a creature ABILITY, which only the Courtyard's colour may fund) then silently no-op'd.
//      MTG_PAY_NARROW_RESTRICTED_FIRST.
//
// Every "defect" case below fails on the pre-fix payer and passes with the levers on; the "lever off"
// cases pin each lever's =0 arm to the historical behaviour, and the "lossless" cases prove a cost that
// is payable ONLY with pain is still paid. The payers are shared by the executor and the rollout
// (TapForCostShared / TapForCostBacktrack / BatchPrepayMainCasts), so one board per shape covers both.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "mtg_test_seam.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsVp()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefVp(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

GameState FreshVp(int turn = 5)
{
    EnsureCardsVp();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    s.m_provider = &SelectDecisionProvider(DeckLoader::LoadFromFile(
        ResolveHeuristicDefaultsPath("decks/Vampires/Vampire.cod")));
    for (int k = 0; k < 20; ++k)
    {
        Card c = DefVp("Swamp").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

// Returns the new permanent's battlefield index.
int PutVp(GameState& s, const std::string& name, int number, bool tapped = false)
{
    Permanent p;
    p.card             = DefVp(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    p.tapped           = tapped;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

void HandVp(GameState& s, const std::string& name, int number)
{
    Card c = DefVp(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

Action CastVp(const GameState& s, const std::string& name, int nth = 0)
{
    const CardDefinition& d = DefVp(name);
    Action a;
    a.kind           = Action::Kind::CastFromHand;
    a.card_name      = d.card.m_name;
    a.def            = &d;
    a.cost           = d.card.m_mana_cost;
    a.is_noncreature = !d.card.IsCreature();
    a.card_mv        = d.card.m_mana_cost.ManaValue();
    int seen = 0;
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i)
    {
        if (s.players[0].hand[static_cast<std::size_t>(i)].m_name != d.card.m_name) { continue; }
        if (seen++ == nth) { a.hand_index = i; break; }
    }
    return a;
}

bool TappedVp(const GameState& s, int idx) { return s.battlefield[static_cast<std::size_t>(idx)].tapped; }

// Pins both payer levers for one scope (-1 = the env default, which is ON), and always restores.
struct PayerArms
{
    PayerArms(int pain, int narrow)
    {
        heurarm::t_arm[heurarm::PAY_PAIN_LAST]               = static_cast<std::int8_t>(pain);
        heurarm::t_arm[heurarm::PAY_NARROW_RESTRICTED_FIRST] = static_cast<std::int8_t>(narrow);
    }
    ~PayerArms()
    {
        heurarm::t_arm[heurarm::PAY_PAIN_LAST]               = -1;
        heurarm::t_arm[heurarm::PAY_NARROW_RESTRICTED_FIRST] = -1;
    }
};

ManaCost Pips(int generic, int b, int r)
{
    ManaCost c;
    c.generic = generic;
    c.black   = b;
    c.red     = r;
    return c;
}

}   // namespace

// ---- F: the greedy (per-cast scarcity path) --------------------------------------------------------

TEST_CASE("payer F (greedy): a creature's {B} takes the painless Courtyard, not the Estate's pay-1-life colour, in either order")
{
    for (int estate_first = 0; estate_first < 2; ++estate_first)
    {
        GameState s = FreshVp();
        int estate, yard;
        if (estate_first) { estate = PutVp(s, "Voldaren Estate", 1); yard = PutVp(s, "Secluded Courtyard", 2); }
        else              { yard = PutVp(s, "Secluded Courtyard", 2); estate = PutVp(s, "Voldaren Estate", 1); }
        INFO("estate_first=" << estate_first);
        REQUIRE(TapForCostDirect(s, Pips(0, 1, 0), /*for_creature=*/true));
        CHECK(s.players[0].life == gamesetup::StartingLife());
        CHECK(TappedVp(s, yard));
        CHECK_FALSE(TappedVp(s, estate));
    }
}

TEST_CASE("payer F (greedy): {1}{B}{B} off Swamp + Estate + Courtyard pays painlessly -- the Estate gives {C} (s91012 T3 shape)")
{
    for (int estate_first = 0; estate_first < 2; ++estate_first)
    {
        GameState s = FreshVp();
        if (estate_first) { PutVp(s, "Voldaren Estate", 1); PutVp(s, "Secluded Courtyard", 2); }
        else              { PutVp(s, "Secluded Courtyard", 2); PutVp(s, "Voldaren Estate", 1); }
        PutVp(s, "Swamp", 3);
        INFO("estate_first=" << estate_first);
        REQUIRE(TapForCostDirect(s, Pips(1, 2, 0), /*for_creature=*/true));
        CHECK(s.players[0].life == gamesetup::StartingLife());
    }
}

TEST_CASE("payer F (greedy): a {B} with Estate + Swamp, either order, is the Swamp's -- no life lost")
{
    for (int estate_first = 0; estate_first < 2; ++estate_first)
    {
        GameState s = FreshVp();
        int estate, swamp;
        if (estate_first) { estate = PutVp(s, "Voldaren Estate", 1); swamp = PutVp(s, "Swamp", 2); }
        else              { swamp = PutVp(s, "Swamp", 2); estate = PutVp(s, "Voldaren Estate", 1); }
        INFO("estate_first=" << estate_first);
        REQUIRE(TapForCostDirect(s, Pips(0, 1, 0), /*for_creature=*/true));
        CHECK(s.players[0].life == gamesetup::StartingLife());
        CHECK(TappedVp(s, swamp));
        CHECK_FALSE(TappedVp(s, estate));
    }
}

TEST_CASE("payer F (greedy): lever off (MTG_PAY_PAIN_LAST=0) restores battlefield order -- the Estate pays and bleeds")
{
    PayerArms arms(/*pain=*/0, /*narrow=*/-1);
    GameState s = FreshVp();
    const int estate = PutVp(s, "Voldaren Estate", 1);
    PutVp(s, "Secluded Courtyard", 2);
    REQUIRE(TapForCostDirect(s, Pips(0, 1, 0), /*for_creature=*/true));
    CHECK_MESSAGE(s.players[0].life == gamesetup::StartingLife() - 1, "precondition: the =0 arm reproduces F");
    CHECK(TappedVp(s, estate));
}

TEST_CASE("payer F (greedy): a fed filter is only as painless as its feed -- no detour through Cascade Bluffs (treasure_hunt d0 gi680 T5)")
{
    // {1}{U} (Treasure Hunt, noncreature) off Fiery Islet (every tap costs 1 life), Cascade Bluffs (a filter
    // whose feed must be {U}/{R}), Reliquary Tower ({C}) and Ferrous Lake (the game's ramp filter, which
    // a Tower can feed -- so the Bluffs is not stranded and MTG_FEED_FILTER_FIRST's reroute stays out). The
    // Islet is the Bluffs' only possible feed, so routing the {U} through the Bluffs costs the same life
    // AND the Bluffs -- the pain band must not prefer it.
    GameState s = FreshVp();
    const int islet = PutVp(s, "Fiery Islet", 1);
    const int bluffs = PutVp(s, "Cascade Bluffs", 2);
    const int tower = PutVp(s, "Reliquary Tower", 3);
    PutVp(s, "Ferrous Lake", 4);
    ManaCost c; c.generic = 1; c.blue = 1;
    REQUIRE(TapForCostDirect(s, c, /*for_creature=*/false));
    CHECK(TappedVp(s, islet));
    CHECK(TappedVp(s, tower));
    CHECK_FALSE(TappedVp(s, bluffs));
    CHECK(s.players[0].life == gamesetup::StartingLife() - 1);
}

// ---- F: the backtracker and the whole-turn prepay ---------------------------------------------------

namespace
{
// seed 91016 T5, the lands in the game's own battlefield order.
struct Board91016
{
    GameState s;
    int cliffs = -1, yard = -1, swamp = -1, estate = -1;
};

Board91016 Make91016()
{
    Board91016 b;
    b.s      = FreshVp(5);
    b.cliffs = PutVp(b.s, "Blackcleave Cliffs", 7);
    PutVp(b.s, "Falkenrath Gorger", 16);
    b.yard   = PutVp(b.s, "Secluded Courtyard", 33);
    PutVp(b.s, "Legion Lieutenant", 24);
    b.swamp  = PutVp(b.s, "Swamp", 44);
    b.estate = PutVp(b.s, "Voldaren Estate", 58);
    PutVp(b.s, "Asylum Visitor", 5);
    HandVp(b.s, "Bloodghast", 3);
    HandVp(b.s, "Falkenrath Pit Fighter", 20);
    HandVp(b.s, "Markov Baron", 26);
    HandVp(b.s, "Stromkirk Captain", 38);
    HandVp(b.s, "Stromkirk Captain", 39);
    return b;
}
}   // namespace

TEST_CASE("payer F (backtracker): the joint {1}{B}{R}{R} solve finds the painless assignment (s91016 T5)")
{
    Board91016 b = Make91016();
    ManaPool produced;
    REQUIRE(TapForCostBacktrack(b.s, Pips(1, 1, 2), /*for_creature=*/true, ManaPool{}, nullptr, nullptr,
                                /*out_leftover=*/nullptr, 0, -1, /*reserved_mask=*/0, &produced));
    CHECK(b.s.players[0].life == gamesetup::StartingLife());
    CHECK(produced.Total() == 4);
}

TEST_CASE("payer F (prepay): {Pit Fighter, Stromkirk Captain} pre-pays painlessly (s91016 T5, plan 48)")
{
    Board91016 b = Make91016();
    const std::vector<Action> line = { CastVp(b.s, "Falkenrath Pit Fighter"), CastVp(b.s, "Stromkirk Captain") };
    REQUIRE(TurnSolver::BatchPrepayMainCasts(b.s, line));
    CHECK(b.s.players[0].life == gamesetup::StartingLife());
    CHECK(b.s.floating_mana.Total() == 4);
}

TEST_CASE("payer F (prepay): lever off reproduces the joint solve's pain (s91016 T5)")
{
    PayerArms arms(/*pain=*/0, /*narrow=*/-1);
    Board91016 b = Make91016();
    const std::vector<Action> line = { CastVp(b.s, "Falkenrath Pit Fighter"), CastVp(b.s, "Stromkirk Captain") };
    REQUIRE(TurnSolver::BatchPrepayMainCasts(b.s, line));
    CHECK_MESSAGE(b.s.players[0].life == gamesetup::StartingLife() - 1, "precondition: the =0 arm reproduces F");
}

TEST_CASE("payer F (prepay shrink): giving a surplus source back never buys it with the Estate's pain")
{
    // {Dire Moon, Dire Moon} = {B}{B} off Courtyard, Estate, Swamp. The joint solve over-taps all three
    // (the Courtyard's generic-representative colour is explored first) and the shrink post-pass then
    // tries to give a source back. Releasing the Courtyard forces the re-solve onto the Estate's
    // coloured mode; the fix refuses that trade and releases the Estate instead.
    GameState s = FreshVp();
    PutVp(s, "Secluded Courtyard", 2);
    const int estate = PutVp(s, "Voldaren Estate", 1);
    PutVp(s, "Swamp", 3);
    HandVp(s, "Vampire of the Dire Moon", 10);
    HandVp(s, "Vampire of the Dire Moon", 11);
    const std::vector<Action> line = { CastVp(s, "Vampire of the Dire Moon", 0), CastVp(s, "Vampire of the Dire Moon", 1) };
    REQUIRE(TurnSolver::BatchPrepayMainCasts(s, line));
    CHECK(s.players[0].life == gamesetup::StartingLife());
    CHECK_FALSE(TappedVp(s, estate));
}

TEST_CASE("payer F (lossless): when pain is the ONLY way, every payer still pays it")
{
    // {B}{B} with Estate + Swamp: the Estate's coloured mode is the second {B}.
    {
        GameState s = FreshVp();
        PutVp(s, "Voldaren Estate", 1);
        PutVp(s, "Swamp", 2);
        CHECK(TapForCostDirect(s, Pips(0, 2, 0), /*for_creature=*/true));
        CHECK(s.players[0].life == gamesetup::StartingLife() - 1);
    }
    {
        GameState s = FreshVp();
        PutVp(s, "Voldaren Estate", 1);
        PutVp(s, "Swamp", 2);
        ManaPool produced;
        CHECK(TapForCostBacktrack(s, Pips(0, 2, 0), /*for_creature=*/true, ManaPool{}, nullptr, nullptr,
                                  nullptr, 0, -1, 0, &produced));
        CHECK(s.players[0].life == gamesetup::StartingLife() - 1);
    }
    {
        GameState s = FreshVp();
        PutVp(s, "Voldaren Estate", 1);
        PutVp(s, "Swamp", 2);
        HandVp(s, "Vampire of the Dire Moon", 10);
        HandVp(s, "Vampire of the Dire Moon", 11);
        const std::vector<Action> line = { CastVp(s, "Vampire of the Dire Moon", 0), CastVp(s, "Vampire of the Dire Moon", 1) };
        CHECK(TurnSolver::BatchPrepayMainCasts(s, line));
        CHECK(s.players[0].life == gamesetup::StartingLife() - 1);
    }
    // ...and a noncreature cost still cannot take the Estate's colour at all (unchanged legality).
    {
        GameState s = FreshVp();
        PutVp(s, "Voldaren Estate", 1);
        CHECK_FALSE(TapForCostDirect(s, Pips(0, 1, 0), /*for_creature=*/false));
        CHECK(s.players[0].life == gamesetup::StartingLife());
    }
}

// ---- L: narrow restricted first ------------------------------------------------------------------

TEST_CASE("payer L (greedy): Pit Fighter's {R} takes the Territory, so its {1}{R} activation can still use the Courtyard")
{
    for (int yard_first = 0; yard_first < 2; ++yard_first)
    {
        GameState s = FreshVp();
        int yard, terr;
        if (yard_first) { yard = PutVp(s, "Secluded Courtyard", 1); terr = PutVp(s, "Unclaimed Territory", 2); }
        else            { terr = PutVp(s, "Unclaimed Territory", 2); yard = PutVp(s, "Secluded Courtyard", 1); }
        PutVp(s, "Swamp", 3);
        INFO("yard_first=" << yard_first);
        REQUIRE(TapForCostDirect(s, Pips(0, 0, 1), /*for_creature=*/true));
        CHECK(TappedVp(s, terr));
        CHECK_FALSE(TappedVp(s, yard));
        CreatureAbilityPayScope scope;
        CHECK(TapForCostDirect(s, Pips(1, 0, 1), /*for_creature=*/false));   // the activation: Courtyard {R} + Swamp
    }
}

TEST_CASE("payer L (greedy): lever off (MTG_PAY_NARROW_RESTRICTED_FIRST=0) -- the Courtyard pays the cast and strands the activation")
{
    PayerArms arms(/*pain=*/-1, /*narrow=*/0);
    GameState s = FreshVp();
    const int yard = PutVp(s, "Secluded Courtyard", 1);
    PutVp(s, "Unclaimed Territory", 2);
    PutVp(s, "Swamp", 3);
    REQUIRE(TapForCostDirect(s, Pips(0, 0, 1), /*for_creature=*/true));
    CHECK_MESSAGE(TappedVp(s, yard), "precondition: the =0 arm reproduces L");
    CreatureAbilityPayScope scope;
    CHECK_FALSE(TapForCostDirect(s, Pips(1, 0, 1), /*for_creature=*/false));
}

TEST_CASE("payer L (prepay shrink): of two equally flexible surplus lands, the Courtyard is the one given back")
{
    // {Pit Fighter {R}, Dire Moon {B}} off Territory, Courtyard, Swamp: the joint solve taps all three and
    // the shrink returns one rainbow land -- the Courtyard, whatever the battlefield order.
    GameState s = FreshVp();
    const int terr = PutVp(s, "Unclaimed Territory", 2);
    const int yard = PutVp(s, "Secluded Courtyard", 1);
    PutVp(s, "Swamp", 3);
    HandVp(s, "Falkenrath Pit Fighter", 10);
    HandVp(s, "Vampire of the Dire Moon", 11);
    const std::vector<Action> line = { CastVp(s, "Falkenrath Pit Fighter"), CastVp(s, "Vampire of the Dire Moon") };
    REQUIRE(TurnSolver::BatchPrepayMainCasts(s, line));
    CHECK(TappedVp(s, terr));
    CHECK_FALSE(TappedVp(s, yard));
}

namespace
{
void ResolveAllVp(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

// seed 91001 T4, post-combat main: the attackers tapped, four lands up (Courtyard first), the opponent
// has lost life, a second Pit Fighter in hand.
GameState Make91001()
{
    GameState s = FreshVp(4);
    PutVp(s, "Falkenrath Gorger", 16, /*tapped=*/true);
    PutVp(s, "Falkenrath Pit Fighter", 20, /*tapped=*/true);
    PutVp(s, "Markov Baron", 26, /*tapped=*/true);
    PutVp(s, "Secluded Courtyard", 33);
    PutVp(s, "Swamp", 44);
    PutVp(s, "Unclaimed Territory", 50);
    PutVp(s, "Voldaren Estate", 58);
    HandVp(s, "Bloodline Keeper", 2);
    HandVp(s, "Drana, Liberator of Malakir", 12);
    HandVp(s, "Falkenrath Pit Fighter", 21);
    HandVp(s, "Stromkirk Captain", 38);
    s.opponent_lost_life_this_turn = true;
    s.players[1].life = 7;
    return s;
}
}   // namespace

TEST_CASE("payer L (search): main 2's 'cast Pit Fighter, then activate it' really draws two, in both worlds (s91001 T4)")
{
    GameState s = Make91001();
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/false);
    const TurnSolver::Plan* found = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        int casts = 0, acts = 0;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::CastFromHand) { ++casts; if (a.card_name.str() != "Falkenrath Pit Fighter") { casts = 99; } }
            if (a.kind == Action::Kind::SacCreatureOutlet) { ++acts; }
        }
        if (casts == 1 && acts == 1) { found = &p; break; }
    }
    REQUIRE_MESSAGE(found != nullptr, "the enumerator no longer offers the cast + activation line");
    TurnSolver::Plan line = *found;
    // The draw opens a hand-entry breakpoint; pin its empty arm in both worlds (the lockstep test's idiom)
    // so the comparison is exactly the plan's own actions.
    if (line.bp_choice < 0) { line.bp_choice = TurnSolver::kBpEmptyChoice; line.bp_all = true; }
    const std::size_t hand0 = s.players[0].hand.size();
    // Cast (-1), discard (-1), draw two (+2): the hand is back to its size only if the activation fired.
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, line, /*is_pre_combat=*/false);
    CHECK(roll.players[0].hand.size() == hand0);
    CHECK(roll.players[0].library.size() == s.players[0].library.size() - 2);
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    eng.SetSearchPostCombat(true);
    MtgTestSeam::CommitPhasePlan(eng, line, false);
    GameState exec = s;
    eng.TakeTurn(exec, false, ResolveAllVp);
    CHECK(exec.players[0].library.size() == s.players[0].library.size() - 2);
    CHECK(exec.players[0].hand.size() == roll.players[0].hand.size());
}
