// Shadowborn Apostle, s7 budget miss follow-up (2026-11-04; docs/design/analysis-Shadowborn Apostle.md,
// "Lake fold + Profane fetch timing"):
//   * ON-DEMAND sac-for-mana (superseded MTG_IDLE_CRACK_FOLD, deleted with the Lake's SacForMana action) --
//     the payer cracks Lake of the Dead / Lotus Bloom only when a cost cannot be paid without it.
//   * MTG_PROFANE_RESOLVE_FAN -- Profane Tutor's off-suspend fetch is decided where the search crosses
//     into the upkeep it resolves at (BoundaryFan), never staged as a main-phase plan variant.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace
{
void EnsureCardsLp()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}
Card FullCardLp(const std::string& name, int number)
{
    const CardDefinition* def = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(def != nullptr, "card not in cards.json: ", name);
    Card c     = def->card;
    c.m_number = number;
    return c;
}
Card PlaceholderLp(const std::string& name, int number)
{
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}
Permanent PermLp(const std::string& name, int number, bool tapped = false)
{
    Permanent p;
    p.card             = FullCardLp(name, number);
    p.controller_index = 0;
    p.owner_index      = 0;
    p.tapped           = tapped;
    return p;
}
const ShadowbornProvider& ProvLp() { static const ShadowbornProvider p; return p; }
GameState BaseLp()
{
    EnsureCardsLp();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.m_provider          = &ProvLp();
    for (int i = 0; i < 20; ++i) { s.players[0].library.push_back(FullCardLp("Swamp", 300 + i)); }
    return s;
}
// A lever pinned for one scope (the per-job batch override), restored on exit.
struct ArmScope
{
    heurarm::Arm saved;
    ArmScope(heurarm::Slot slot, bool on) : saved(heurarm::t_arm) { heurarm::t_arm[slot] = on ? 1 : 0; }
    ~ArmScope() { heurarm::t_arm = saved; }
};
bool PlanCracks(const TurnSolver::Plan& p)
{
    for (const Action& a : p.actions) { if (a.kind == Action::Kind::SacForMana) { return true; } }
    return false;
}
int CountCrackPlans(const std::vector<TurnSolver::Plan>& v)
{ return static_cast<int>(std::count_if(v.begin(), v.end(), PlanCracks)); }
}   // namespace

// ---- ON-DEMAND sac-for-mana (docs/design/sac-for-mana-on-demand.md, USER 2026-11-04: "it should be on-demand
// and only done that way"). The payer cracks Lake of the Dead / Lotus Bloom when the cost needs it or
// a searched Lake policy requests the early crack; no plan carries a SacForMana action for either.
namespace
{
int CountPerm(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}
bool PayLp(GameState& s, int generic, int black, int red = 0)
{
    ManaCost c; c.generic = generic; c.black = black; c.red = red;
    return TapForCostShared(s, c, /*for_creature=*/false, nullptr, true);
}
}   // namespace

TEST_CASE("On-demand Lake: not cracked when the lands pay, and its plain tap is the last resort")
{
    GameState s = BaseLp();
    s.battlefield.push_back(PermLp("Lake of the Dead", 80));
    for (int i = 0; i < 3; ++i) { s.battlefield.push_back(PermLp("Swamp", 81 + i)); }
    REQUIRE(PayLp(s, 1, 1));                     // {1}{B}
    CHECK(CountPerm(s, "Swamp") == 3);           // no Swamp sacrificed
    CHECK_FALSE(s.battlefield[0].tapped);        // the Lake held: its crack stays available
    CHECK(s.floating_mana.Total() == 0);
}

TEST_CASE("On-demand Lake: cracked when the cost needs it, and the surplus floats")
{
    GameState s = BaseLp();
    s.battlefield.push_back(PermLp("Lake of the Dead", 80));
    s.battlefield.push_back(PermLp("Swamp", 81));
    // Plain: Lake + Swamp = 2. Crack: the Swamp's own {B} (tapped before it goes) + {B}{B}{B}{B} = 5.
    REQUIRE(PayLp(s, 2, 1));                     // {2}{B}
    CHECK(CountPerm(s, "Swamp") == 0);
    CHECK(CountPerm(s, "Lake of the Dead") == 1);
    CHECK(s.floating_mana.Total() == 2);         // leftover rule: the rest floats for the phase
}

TEST_CASE("On-demand Lake: searched early crack preserves mana for later payments")
{
    GameState ordinary = BaseLp();
    ordinary.battlefield.push_back(PermLp("Lake of the Dead", 80));
    ordinary.battlefield.push_back(PermLp("Swamp", 81));
    GameState searched = ordinary;
    searched.scripted_early_ondemand_crack = true;
    REQUIRE(PayLp(ordinary, 0, 1));
    REQUIRE(PayLp(searched, 0, 1));
    CHECK(CountPerm(ordinary, "Swamp") == 1);
    CHECK_FALSE(ordinary.battlefield[0].tapped);
    CHECK(CountPerm(searched, "Swamp") == 0);
    CHECK(searched.battlefield[0].tapped);
    CHECK(searched.floating_mana.Total() == 4);
    CHECK_FALSE(searched.scripted_early_ondemand_crack);
}

TEST_CASE("On-demand Lake: the crack and the plain tap are exclusive")
{
    GameState s = BaseLp();
    s.battlefield.push_back(PermLp("Lake of the Dead", 80));
    s.battlefield.push_back(PermLp("Swamp", 81));
    const GameState before = s;
    CHECK_FALSE(PayLp(s, 4, 2));                 // 6 > 5: plain {B} on top of the crack is not on offer
    CHECK(s.battlefield.size() == before.battlefield.size());
    CHECK(s.floating_mana.Total() == 0);
    for (std::size_t i = 0; i < s.battlefield.size(); ++i) { CHECK_FALSE(s.battlefield[i].tapped); }
}

TEST_CASE("On-demand Lotus Bloom: kept when the lands pay; pays a colour no land makes")
{
    GameState s = BaseLp();
    s.battlefield.push_back(PermLp("Lotus Bloom", 70));
    s.battlefield.push_back(PermLp("Swamp", 71));
    s.battlefield.push_back(PermLp("Swamp", 72));
    REQUIRE(PayLp(s, 1, 1));
    CHECK(CountPerm(s, "Lotus Bloom") == 1);
    GameState t = BaseLp();
    t.battlefield.push_back(PermLp("Lotus Bloom", 70));
    t.battlefield.push_back(PermLp("Swamp", 71));
    REQUIRE(PayLp(t, 0, 1, 2));                  // {B}{R}{R}: the red only the Lotus can make
    CHECK(CountPerm(t, "Lotus Bloom") == 0);
    CHECK(t.floating_mana.red == 1);             // three of ONE colour: one red floats on
    CHECK(t.floating_mana.Total() == 1);
}

TEST_CASE("On-demand: no SacForMana plan action for Lake / Lotus, and a cast that needs the crack is offered")
{
    // The enumeration is tested alone: the Shadowborn main-1 rule (default ON) folds main-1 casts, and the
    // ritual-payoff prune drops plans on its own, so both are pinned off here.
    ArmScope rp(heurarm::SB_RITUAL_PAYOFF, false), m1r(heurarm::SB_MAIN1_RULE, false);
    GameState s = BaseLp();
    s.battlefield.push_back(PermLp("Lake of the Dead", 80));
    for (int i = 0; i < 3; ++i) { s.battlefield.push_back(PermLp("Swamp", 81 + i)); }
    s.battlefield.push_back(PermLp("Lotus Bloom", 85));
    s.players[0].hand.push_back(FullCardLp("Abhorrent Overlord", 86));   // {5}{B}{B}
    const std::vector<TurnSolver::Plan> all = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    CHECK(CountCrackPlans(all) == 0);
    bool overlord = false;
    for (const TurnSolver::Plan& p : all)
    {
        for (const Action& x : p.actions)
        { if (x.kind == Action::Kind::CastFromHand && x.card_name.str() == "Abhorrent Overlord") { overlord = true; } }
    }
    CHECK(overlord);
}

namespace
{
GameState ProfaneDueState()
{
    GameState s = BaseLp();
    s.players[0].library = Library{};
    s.players[0].library.push_back(FullCardLp("Dark Ritual", 90));
    s.players[0].library.push_back(FullCardLp("Demonic Tutor", 91));
    s.players[0].library.push_back(FullCardLp("Culling the Weak", 92));
    s.players[0].library.push_back(FullCardLp("Thrumming Stone", 94));
    s.players[0].library.push_back(FullCardLp("Shadowborn Apostle", 95));
    for (int i = 0; i < 10; ++i) { s.players[0].library.push_back(FullCardLp("Swamp", 300 + i)); }
    s.battlefield.push_back(PermLp("Swamp", 81));
    s.battlefield.push_back(PermLp("Swamp", 82));
    SuspendedCard sc;
    sc.card        = FullCardLp("Profane Tutor", 93);
    sc.arrive_turn = s.TurnSeq() + 1;   // cast off suspend at the NEXT upkeep
    s.players[0].suspended_cards.push_back(sc);
    return s;
}
}   // namespace

TEST_CASE("Profane fetch: CastOffSuspend reads scripted_suspend_tutor, a rank into the resolution candidates")
{
    ArmScope a(heurarm::PROFANE_RESOLVE_FAN, true);
    for (int pin : { -1, 0, 1 })
    {
        GameState s = ProfaneDueState();
        s.scripted_suspend_tutor = static_cast<std::int16_t>(pin);
        const std::vector<std::string> cands =
            RankedTutorCandidates(ProvLp(), s, 0, CardDatabase::Instance().Lookup("Profane Tutor")->params);
        REQUIRE(cands.size() >= 2);
        const std::string want = cands[pin < 0 ? 0 : static_cast<std::size_t>(pin)];
        SuspendTutorProbe pr;
        t_suspend_tutor_probe = &pr;
        CastOffSuspend(s, 0, FullCardLp("Profane Tutor", 93));
        t_suspend_tutor_probe = nullptr;
        REQUIRE(s.players[0].hand.size() == 1);
        CHECK(s.players[0].hand[0].m_name.str() == want);
        CHECK(s.scripted_suspend_tutor == -1);   // consumed
        CHECK(pr.n == 1);
        CHECK(pr.size[0] >= 2);
        CHECK(s.scripted_upkeep_land_search == -1);   // the land-search stream is not read
    }
}

TEST_CASE("Profane fetch: the main-phase menu stages no fetch; the crossing fans every candidate")
{
    ArmScope a(heurarm::PROFANE_RESOLVE_FAN, true);
    GameState s = ProfaneDueState();
    const std::vector<TurnSolver::Plan> menu = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    REQUIRE_FALSE(menu.empty());
    for (const TurnSolver::Plan& p : menu)
    {
        CHECK(p.upkeep_land_search_choice == -1);
        CHECK(p.suspend_tutor_choice == -1);
    }
    // At the crossing: one crossing per resolution-state candidate (provider front first), each fetching a
    // DIFFERENT card, and the recorded pin is the rank that crossing took.
    const std::vector<std::pair<int, std::string>> x = TurnSolver::SuspendTutorBoundaryForTest(s);
    const std::vector<std::string> cands =
        RankedTutorCandidates(ProvLp(), s, 0, CardDatabase::Instance().Lookup("Profane Tutor")->params);
    std::set<std::string> uniq(cands.begin(), cands.end());
    const std::size_t width = std::min<std::size_t>(uniq.size(), static_cast<std::size_t>(ProvLp().TutorSearchWidth()));
    REQUIRE(width >= 2);
    REQUIRE(x.size() == width);
    std::set<std::string> fetched;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        CHECK(x[i].first == static_cast<int>(i));
        fetched.insert(x[i].second);
    }
    CHECK(fetched.size() == width);
    CHECK(x[0].second == cands[0]);   // the provider's front first: it keeps every tie
}

TEST_CASE("Profane fetch: lever OFF restores the main-phase upkeep pin")
{
    {
        ArmScope a(heurarm::PROFANE_RESOLVE_FAN, false);
        GameState s = ProfaneDueState();
        const std::vector<TurnSolver::Plan> menu = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
        bool staged = false;
        for (const TurnSolver::Plan& p : menu) { if (p.upkeep_land_search_choice > 0) { staged = true; } }
        CHECK(staged);
        CHECK(TurnSolver::SuspendTutorBoundaryForTest(s).size() == 1);   // one plain crossing
    }
}
