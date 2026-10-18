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

// ---- F x L: pain-last must not spend the only ability-legal colour on a cast (the integrator's follow-up) --

TEST_CASE("payer F x L (search): cast Gorger off the Estate's pain so Pit Fighter's {1}{R} keeps the Courtyard, both worlds")
{
    // The sub-integrator's open question 1: Secluded Courtyard + Voldaren Estate + Swamp, a Falkenrath
    // Pit Fighter on the battlefield, main 2 after combat damage. "Cast Falkenrath Gorger {R}, then
    // activate Pit Fighter {1}{R}": the activation's {R} can come ONLY from the Courtyard (the Estate's
    // colour is creature-SPELL mana, the Swamp makes none), so the Gorger must take the Estate's painful
    // {R}. Pain-last alone sent the cast to the Courtyard and the activation silently no-op'd; the
    // activation-line hold now reserves an ability-legal provider for the activation's own pips
    // (PlanTraits::act_pips_cab).
    GameState s = FreshVp(5);
    s.phase = Phase::PostCombatMain;
    s.opponent_lost_life_this_turn = true;
    PutVp(s, "Falkenrath Pit Fighter", 20);
    PutVp(s, "Vampire of the Dire Moon", 55);
    PutVp(s, "Secluded Courtyard", 33);
    PutVp(s, "Voldaren Estate", 58);
    PutVp(s, "Swamp", 44);
    HandVp(s, "Falkenrath Gorger", 16);
    HandVp(s, "Swamp", 45);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/false);
    const TurnSolver::Plan* found = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        int gorger = 0, acts = 0, other = 0;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Falkenrath Gorger") { ++gorger; }
            else if (a.kind == Action::Kind::SacCreatureOutlet) { ++acts; }
            else { ++other; }
        }
        if (gorger == 1 && acts == 1 && other == 0) { found = &p; break; }
    }
    REQUIRE_MESSAGE(found != nullptr, "the enumerator does not offer cast Gorger + activate Pit Fighter");
    TurnSolver::Plan line = *found;
    if (line.bp_choice < 0) { line.bp_choice = TurnSolver::kBpEmptyChoice; line.bp_all = true; }
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, line, /*is_pre_combat=*/false);
    CHECK(roll.players[0].library.size() == s.players[0].library.size() - 2);   // Pit Fighter drew two
    CHECK(roll.players[0].life == s.players[0].life - 1);                       // the Estate's {R}, forced
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    eng.SetSearchPostCombat(true);
    MtgTestSeam::CommitPhasePlan(eng, line, false);
    GameState exec = s;
    eng.TakeTurn(exec, false, ResolveAllVp);
    CHECK(exec.players[0].library.size() == s.players[0].library.size() - 2);
    CHECK(exec.players[0].life == roll.players[0].life);
}

// ---- the payer A/B follow-ups (2026-10-18: the slower games of the pooled held-out A/B) -----------------

namespace
{
// vampires std d3 b10 / d5 b20 block B gi129 (seed 13480130), the T4 horizon-leaf board: three Estates and
// the Courtyard just played, Legion Lieutenant + Stromkirk Captain able to attack, Pit Fighter in hand.
GameState MakeGi129T4(bool with_attacker)
{
    GameState s = FreshVp(4);
    s.phase = Phase::PreCombatMain;
    PutVp(s, "Voldaren Estate", 58);
    PutVp(s, "Voldaren Estate", 59);
    if (with_attacker) { PutVp(s, "Legion Lieutenant", 24); }
    PutVp(s, "Voldaren Estate", 60);
    if (with_attacker) { PutVp(s, "Stromkirk Captain", 37); }
    PutVp(s, "Secluded Courtyard", 33);
    HandVp(s, "Falkenrath Pit Fighter", 20);
    HandVp(s, "Swamp", 44);
    return s;
}

int IndexOfVp(const GameState& s, int number)
{
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { if (s.battlefield[static_cast<std::size_t>(i)].card.m_number == number) { return i; } }
    return -1;
}

TurnSolver::Plan CastOnlyPlanVp(const GameState& s, const std::string& name)
{
    TurnSolver::Plan p;
    p.actions.push_back(CastVp(s, name));
    p.land_decided = true;   // the land drop is already made (lands_played_this_turn = 1)
    return p;
}
}   // namespace

TEST_CASE("payer A/B gi129 (search): a main-1 attack turn casts Pit Fighter off the Estate's pain, keeping the Courtyard for its main-2 draw-two, both worlds")
{
    // Pit Fighter's "{1}{R}, discard, sacrifice a Vampire: draw two" needs an opponent to have lost life,
    // so it is always a MAIN-2 plan and the main-1 plan never carries it. Pain-last alone paid the cast's
    // {R} with the Courtyard -- the only land here whose colour may pay a creature ABILITY -- and the
    // main-2 activation had no legal {R}: gi129's horizon leaf lost its T5 kill, and the budgeted root
    // then skipped its turn-1 land. PlanTraits now owes the outlet's pips as creature-ability demand.
    GameState s = MakeGi129T4(/*with_attacker=*/true);
    const TurnSolver::Plan line = CastOnlyPlanVp(s, "Falkenrath Pit Fighter");
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, line, /*is_pre_combat=*/true);
    REQUIRE(IndexOfVp(roll, 20) >= 0);                                           // Pit Fighter resolved
    CHECK_FALSE(TappedVp(roll, IndexOfVp(roll, 33)));                            // the Courtyard is kept
    CHECK(roll.players[0].life == s.players[0].life - 1);                         // the Estate's {R}
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, line, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, ResolveAllVp);
    REQUIRE(IndexOfVp(exec, 20) >= 0);
    CHECK_FALSE(TappedVp(exec, IndexOfVp(exec, 33)));
    CHECK(exec.players[0].life == roll.players[0].life);
    // ...and after combat the activation's {R} is the Courtyard's (it would be unpayable otherwise).
    roll.phase = Phase::PostCombatMain;
    roll.opponent_lost_life_this_turn = true;
    CreatureAbilityPayScope scope;
    CHECK(TapForCostDirect(roll, Pips(1, 0, 1), /*for_creature=*/false));
}

TEST_CASE("payer A/B gi129: scoped -- no attacker (the restriction cannot arm) or MTG_PAY_NARROW_RESTRICTED_FIRST=0 keeps pain-last's painless Courtyard")
{
    {
        GameState s = MakeGi129T4(/*with_attacker=*/false);
        GameState roll = s;
        TurnSolver::ApplyPlan(roll, CastOnlyPlanVp(s, "Falkenrath Pit Fighter"), /*is_pre_combat=*/true);
        CHECK(TappedVp(roll, IndexOfVp(roll, 33)));
        CHECK(roll.players[0].life == s.players[0].life);
    }
    {
        PayerArms arms(/*pain=*/-1, /*narrow=*/0);
        GameState s = MakeGi129T4(/*with_attacker=*/true);
        GameState roll = s;
        TurnSolver::ApplyPlan(roll, CastOnlyPlanVp(s, "Falkenrath Pit Fighter"), /*is_pre_combat=*/true);
        CHECK_MESSAGE(TappedVp(roll, IndexOfVp(roll, 33)), "precondition: the =0 arm reproduces gi129's leaf");
        CHECK(roll.players[0].life == s.players[0].life);
    }
}

namespace
{
ManaCost GreenPip() { ManaCost g; g.green = 1; return g; }   // Crop Rotation's {G}

// creature_giving std d0 block B gi24 (seed 13480025) T4: City of Brass + two Forbidden Orchards up, Birds,
// Crop Rotation and Sylvan Scrying in hand, Orchards left in the library to fetch.
GameState MakeGi24T4()
{
    EnsureCardsVp();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 0;
    s.m_provider = &SelectDecisionProvider(DeckLoader::LoadFromFile(
        ResolveHeuristicDefaultsPath("decks/Creature Giving/Creature Giving.cod")));
    for (int k = 0; k < 6; ++k)
    {
        Card c = DefVp("Forbidden Orchard").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    PutVp(s, "City of Brass", 7);
    PutVp(s, "Forbidden Orchard", 26);
    PutVp(s, "Forbidden Orchard", 24);
    HandVp(s, "Birds of Paradise", 3);
    HandVp(s, "Crop Rotation", 11);
    HandVp(s, "Sylvan Scrying", 50);
    return s;
}
}   // namespace

TEST_CASE("payer A/B gi24 (sac-land victim pays first): Crop Rotation's {G} taps the City it sacrifices, leaving two lands for Sylvan Scrying, both worlds")
{
    // Pain-last makes City of Brass the last source tapped, so Birds' and Crop Rotation's {G} came off the
    // Orchards; the additional cost (tapped-first, Orchard last) then sacrificed the UNTAPPED City, and the
    // plan's Sylvan Scrying {1}{G} -- payable by the walk, which never models the sacrifice -- found one
    // untapped land. The victim now pays: the City's {G} (1 life) for Crop Rotation, then it is sacrificed.
    auto untapped_lands = [](const GameState& st)
    {
        int n = 0;
        for (const Permanent& q : st.battlefield) { if (q.card.IsLand() && !q.tapped) { ++n; } }
        return n;
    };
    for (int pain = -1; pain <= 0; ++pain)
    {
        PayerArms arms(pain, /*narrow=*/-1);
        GameState s = MakeGi24T4();
        TurnSolver::Plan line;
        line.land_decided = true;
        line.actions = { CastVp(s, "Birds of Paradise"), CastVp(s, "Crop Rotation") };
        line.actions[1].sacrifice_land = true;   // as the enumerator stamps it (the rollout sacrifices on the flag)
        auto check = [&](const GameState& st, const std::string& world)
        {
            std::string bf, gy;
            for (const Permanent& q : st.battlefield) { bf += q.card.m_name.str() + (q.tapped ? "(T) " : " "); }
            for (const Card& c : st.players[0].graveyard) { gy += c.m_name.str() + " "; }
            INFO(world << " pain=" << pain << " bf: " << bf << "| gy: " << gy << "| life " << st.players[0].life);
            CHECK(IndexOfVp(st, 7) < 0);                                        // the City was the victim
            if (pain != 0)
            {
                CHECK(untapped_lands(st) == 2);                                  // Scrying's {1}{G} is still up
                CHECK(st.players[0].life == gamesetup::StartingLife() - 1);      // the City's tap
            }
            else
            {
                // The historical payer by battlefield order: the City (first) pays Birds, Crop Rotation
                // then sacrifices it TAPPED -- the same two lands, as the off arm of gi24 played it.
                CHECK(untapped_lands(st) == 2);
            }
        };
        GameState roll = s;
        TurnSolver::ApplyPlan(roll, line, /*is_pre_combat=*/true);
        check(roll, "rollout");
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::CommitPhasePlan(eng, line, true);
        GameState exec = s;
        eng.TakeTurn(exec, true, ResolveAllVp);
        check(exec, "executor");
    }
}

TEST_CASE("payer A/B gi24: without the victim promotion, pain-last strands the City (the defect's precondition)")
{
    // The payer half alone, on the same board: with nothing published, pain-last pays both {G} pips off the
    // Orchards and leaves the City -- the land the cost will take -- untapped; published, the City pays.
    GameState s = MakeGi24T4();
    CHECK(SacLandPayVictim(s, GreenPip()) == 7);
    {
        PaySacVictimScope none(0);
        GameState t = s;
        ManaCost g; g.green = 1;
        REQUIRE(TapForCostDirect(t, g, /*for_creature=*/true));     // Birds
        REQUIRE(TapForCostDirect(t, g, /*for_creature=*/false));    // Crop Rotation, no victim published
        CHECK_MESSAGE(!t.battlefield[static_cast<std::size_t>(IndexOfVp(t, 7))].tapped,
                      "precondition: pain-last leaves the City (the victim) untapped");
    }
    {
        GameState t = s;
        ManaCost g; g.green = 1;
        REQUIRE(TapForCostDirect(t, g, /*for_creature=*/true));
        PaySacVictimScope victim(SacLandPayVictim(t, GreenPip()));
        REQUIRE(TapForCostDirect(t, g, /*for_creature=*/false));
        CHECK(t.battlefield[static_cast<std::size_t>(IndexOfVp(t, 7))].tapped);
    }
}

TEST_CASE("payer A/B gi24: MTG_PAY_PAIN_LAST=0 is the historical payment (the predictor publishes nothing)")
{
    PayerArms arms(/*pain=*/0, /*narrow=*/-1);
    GameState s = MakeGi24T4();
    CHECK(SacLandPayVictim(s, GreenPip()) == 0);
}

TEST_CASE("payer A/B gi24: SacLandPayVictim names the ranking's untapped front, and nothing when the front is already tapped")
{
    GameState s = MakeGi24T4();
    CHECK(SacLandPayVictim(s, GreenPip()) == 7);                     // City of Brass: fungible band, Orchards last
    s.battlefield[static_cast<std::size_t>(IndexOfVp(s, 7))].tapped = true;
    CHECK(SacLandPayVictim(s, GreenPip()) == 0);                     // a tapped front is already free
    // An Island beside the City: less flexible, but it cannot pay Crop Rotation's {G}, so the City is
    // still the land promoted (and for a generic pip the Island would be).
    GameState u = MakeGi24T4();
    PutVp(u, "Island", 40);
    CHECK(SacLandPayVictim(u, GreenPip()) == 7);
    ManaCost one; one.generic = 1;
    CHECK(SacLandPayVictim(u, one) == 40);
}

TEST_CASE("payer A/B gi1565: the promoted victim is the LEAST flexible best-band land -- Crop Rotation eats the Forest, never the Stomping Ground, both worlds and both arms")
{
    // creature_giving std d0 block A seed 13181566 T2: Stomping Ground (fetched T1) and the Forest just
    // played, Crop Rotation in hand. The scarcity payer taps the Forest for {G} and tapped-first then
    // sacrifices it -- the historical line. The first build of the victim promotion published the
    // ranking's FRONT (battlefield order: the Stomping Ground), tapped and sacrificed the dual, and the
    // game slipped T7 -> T8.
    for (int pain = -1; pain <= 0; ++pain)
    {
        PayerArms arms(pain, /*narrow=*/-1);
        GameState s = MakeGi24T4();
        s.battlefield.clear();
        s.players[0].hand.clear();
        s.turn_number = 2;
        PutVp(s, "Stomping Ground", 50);
        PutVp(s, "Forest", 28);
        HandVp(s, "Crop Rotation", 12);
        if (pain != 0) { CHECK(SacLandPayVictim(s, GreenPip()) == 28); }
        TurnSolver::Plan line;
        line.land_decided = true;
        line.actions = { CastVp(s, "Crop Rotation") };
        line.actions[0].sacrifice_land = true;
        auto check = [&](const GameState& st, const std::string& world)
        {
            INFO(world << " pain=" << pain);
            CHECK(IndexOfVp(st, 28) < 0);                                        // the Forest was eaten
            REQUIRE(IndexOfVp(st, 50) >= 0);
            CHECK_FALSE(st.battlefield[static_cast<std::size_t>(IndexOfVp(st, 50))].tapped);
        };
        GameState roll = s;
        TurnSolver::ApplyPlan(roll, line, /*is_pre_combat=*/true);
        check(roll, "rollout");
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::CommitPhasePlan(eng, line, true);
        GameState exec = s;
        eng.TakeTurn(exec, true, ResolveAllVp);
        check(exec, "executor");
    }
}
