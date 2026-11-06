// Shadowborn Apostle, s7 budget miss follow-up (2026-11-04; docs/design/analysis-Shadowborn Apostle.md,
// "Lake fold + Profane fetch timing"):
//   * MTG_IDLE_CRACK_FOLD -- a plan that cracks Lake of the Dead for mana it never needs folds onto the
//     same plan without the crack (a SOUND identity fold), and a crack whose mana a cast spends is kept.
//   * MTG_PROFANE_RESOLVE_FAN -- Profane Tutor's off-suspend fetch is decided where the search crosses
//     into the upkeep it resolves at (BoundaryFan), never staged as a main-phase plan variant.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
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

TEST_CASE("Idle crack fold: a Lake crack nothing spends is not offered; with the lever off it is")
{
    // The fold is measured alone: the Shadowborn ritual-payoff prune (default ON) drops an unspent crack too, and the
    // main-1 rule folds main-1 casts, so both are pinned off here.
    ArmScope rp(heurarm::SB_RITUAL_PAYOFF, false), m1r(heurarm::SB_MAIN1_RULE, false);
    GameState s = BaseLp();
    s.battlefield.push_back(PermLp("Lake of the Dead", 80));
    s.battlefield.push_back(PermLp("Swamp", 81));
    s.players[0].hand.push_back(PlaceholderLp("Abhorrent Overlord", 82));   // {5}{B}{B}: unreachable either way
    int off = 0, on = 0;
    {
        ArmScope a(heurarm::IDLE_CRACK_FOLD, false);
        off = CountCrackPlans(TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true));
    }
    long long t0 = 0, f0 = 0, t1 = 0, f1 = 0;
    TurnSolver::IdleCrackFoldCounters(&t0, &f0);
    {
        ArmScope a(heurarm::IDLE_CRACK_FOLD, true);
        on = CountCrackPlans(TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true));
    }
    TurnSolver::IdleCrackFoldCounters(&t1, &f1);
    CHECK(off >= 1);   // the waste exists without the fold
    CHECK(on == 0);    // ...and every copy of it folds onto its crack-less sibling
    CHECK(f1 > f0);
}

TEST_CASE("Idle crack fold: the identity test keeps a crack whose float pays a cast, and needs the sibling")
{
    // The fold is measured alone: the Shadowborn ritual-payoff prune (default ON) drops an unspent crack too, and the
    // main-1 rule folds main-1 casts, so both are pinned off here.
    ArmScope rp(heurarm::SB_RITUAL_PAYOFF, false), m1r(heurarm::SB_MAIN1_RULE, false);
    GameState s = BaseLp();
    s.battlefield.push_back(PermLp("Lake of the Dead", 80));
    s.battlefield.push_back(PermLp("Swamp", 81));
    s.battlefield.push_back(PermLp("Swamp", 82));
    s.players[0].hand.push_back(PlaceholderLp("Demonic Tutor", 83));
    ArmScope a(heurarm::IDLE_CRACK_FOLD, true);
    std::vector<TurnSolver::Plan> all;
    {
        ArmScope off(heurarm::IDLE_CRACK_FOLD, false);
        all = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    }
    // The crack + Demonic Tutor plan leaves a Swamp UNTAPPED and three {B} floating; its sibling taps both Swamps:
    // the end states differ, so the fold keeps it (the float can fund the tutored card's cast this phase).
    std::vector<TurnSolver::Plan> crack_dt;
    for (const TurnSolver::Plan& p : all)
    {
        bool dt = false;
        for (const Action& x : p.actions) { if (x.kind == Action::Kind::CastFromHand && x.card_name.str() == "Demonic Tutor") { dt = true; } }
        if (dt && PlanCracks(p)) { crack_dt.push_back(p); }
    }
    REQUIRE_FALSE(crack_dt.empty());
    const std::vector<TurnSolver::Plan> folded = TurnSolver::FoldIdleLandCracksForTest(s, true, all);
    for (const TurnSolver::Plan& p : crack_dt) { CHECK(std::find(folded.begin(), folded.end(), p) != folded.end()); }
    // A lone crack plan (no sibling without it) is never folded onto nothing.
    std::vector<TurnSolver::Plan> lone;
    for (const TurnSolver::Plan& p : all) { if (PlanCracks(p) && p.actions.size() == 1) { lone.push_back(p); } }
    REQUIRE(lone.size() == 1);
    CHECK(TurnSolver::FoldIdleLandCracksForTest(s, true, lone).size() == 1);
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
        const int rank = pin;
        s.scripted_suspend_tutor = static_cast<std::int16_t>(pin < 0 ? -1 : SuspendTutorPinPack(1, &rank));
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
        const int ri = static_cast<int>(i);
        CHECK(x[i].first == SuspendTutorPinPack(1, &ri));   // rank i, covering the one search
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

TEST_CASE("Profane fetch: the executor's crossing node searches an unpinned fetch and pins it for the upkeep")
{
    // USER 2026-11-04: "We should search and use a heuristic on that decision like any other." The real turn's end
    // decides a fetch the committed line left unpinned (TurnSolver::ChooseRealCrossingFetch), over the same
    // resolution-state ranks the BoundaryFan crosses.
    ArmScope a(heurarm::PROFANE_RESOLVE_FAN, true);
    const std::size_t width = TurnSolver::SuspendTutorBoundaryForTest(ProfaneDueState()).size();
    REQUIRE(width >= 2);
    {
        ArmScope n(heurarm::PROFANE_REAL_NODE, true);
        GameState s = ProfaneDueState();
        const int pin = TurnSolver::ChooseRealCrossingFetch(s, /*depth=*/2, /*max_turns=*/8, nullptr, true);
        REQUIRE(pin >= 0);
        CHECK(SuspendTutorPinCover(pin) == 1);
        CHECK((pin & kLandSearchPinMask) < static_cast<int>(width));
        CHECK(s.scripted_suspend_tutor == pin);
        // Deterministic: the same state decides the same rank.
        GameState s2 = ProfaneDueState();
        CHECK(TurnSolver::ChooseRealCrossingFetch(s2, 2, 8, nullptr, true) == pin);
        // A fetch already pinned (the committed line's crossing recorded one) is covered: nothing to decide.
        GameState sp = ProfaneDueState();
        const int one = 1;
        const int covered = SuspendTutorPinPack(1, &one);
        sp.scripted_suspend_tutor = static_cast<std::int16_t>(covered);
        CHECK(TurnSolver::ChooseRealCrossingFetch(sp, 2, 8, nullptr, true) == -2);
        CHECK(sp.scripted_suspend_tutor == covered);
        // Depth 0 (the d0 runner) keeps the front: no node.
        GameState s0 = ProfaneDueState();
        CHECK(TurnSolver::ChooseRealCrossingFetch(s0, 0, 8, nullptr, true) == -2);
        CHECK(s0.scripted_suspend_tutor == -1);
        // Nothing due: no node.
        GameState sn = ProfaneDueState();
        sn.players[0].suspended_cards.clear();
        CHECK(TurnSolver::ChooseRealCrossingFetch(sn, 2, 8, nullptr, true) == -2);
    }
    {
        ArmScope n(heurarm::PROFANE_REAL_NODE, false);   // the old fallback: the upkeep takes the front
        GameState s = ProfaneDueState();
        CHECK(TurnSolver::ChooseRealCrossingFetch(s, 2, 8, nullptr, true) == -2);
        CHECK(s.scripted_suspend_tutor == -1);
    }
}

TEST_CASE("Profane fetch: the suspended-tutor pin carries a cover, and a covered front is not 'unpinned'")
{
    const int ranks[2] = { 3, 0 };
    int pin = SuspendTutorPinPack(2, ranks);
    CHECK(SuspendTutorPinCover(pin) == 2);
    CHECK_FALSE(SuspendTutorPinIsFront(pin));
    CHECK(ConsumeSuspendTutorPin(pin) == 3);
    CHECK(SuspendTutorPinCover(pin) == 1);
    CHECK(ConsumeSuspendTutorPin(pin) == 0);   // a DECIDED front, not an uncovered search
    CHECK(pin == -1);
    CHECK(ConsumeSuspendTutorPin(pin) == -1);   // past the cover: uncovered
    const int zero = 0;
    CHECK(SuspendTutorPinIsFront(SuspendTutorPinPack(1, &zero)));
    // Two due arrivals against a one-ordinal pin: the second is still to be decided at the crossing.
    ArmScope a(heurarm::PROFANE_RESOLVE_FAN, true);
    GameState s = ProfaneDueState();
    s.scripted_suspend_tutor = static_cast<std::int16_t>(SuspendTutorPinPack(1, &zero));
    CHECK(TurnSolver::SuspendTutorBoundaryForTest(s).size() == 1);   // covered: one plain crossing
    SuspendedCard sc2 = s.players[0].suspended_cards[0];
    sc2.card = FullCardLp("Profane Tutor", 96);
    s.players[0].suspended_cards.push_back(sc2);
    CHECK(TurnSolver::SuspendTutorBoundaryForTest(s).size() >= 2);   // the second arrival is fanned
}

namespace
{
int g_lp_node_calls = 0;
int LpNodeReturnsOne(void*, GameState&, int, const CardParams&, const Card&, const SuspendArrivalCtx&)
{ ++g_lp_node_calls; return 1; }
}   // namespace

TEST_CASE("Profane fetch: the executor's resolution node decides an UNCOVERED search at resolution")
{
    // A ripple free copy can take the pinned rank and leave the arrival's own search uncovered (Shadowborn seed
    // 940184 T5). The real upkeep's tutor branch then asks the node, for the REAL state only.
    ArmScope a(heurarm::PROFANE_RESOLVE_FAN, true);
    const CardParams& pp = CardDatabase::Instance().Lookup("Profane Tutor")->params;
    for (int mode = 0; mode < 3; ++mode)   // 0 = uncovered real state, 1 = covered, 2 = another state (a copy)
    {
        GameState s = ProfaneDueState();
        const Card card = s.players[0].suspended_cards[0].card;
        const std::vector<SuspendedCard> snap = s.players[0].suspended_cards;
        const std::vector<SuspendedCard> rem;
        s.players[0].suspended_cards.clear();
        const int zero = 0;
        if (mode == 1) { s.scripted_suspend_tutor = static_cast<std::int16_t>(SuspendTutorPinPack(1, &zero)); }
        const std::vector<std::string> cands = RankedTutorCandidates(ProvLp(), s, 0, pp);
        REQUIRE(cands.size() >= 2);
        GameState other;
        const SuspendArrivalCtx ctx{ &snap, 0, &rem };
        g_lp_node_calls = 0;
        t_real_stut_node = &LpNodeReturnsOne; t_real_stut_state = (mode == 2) ? &other : &s; t_suspend_arrival_ctx = &ctx;
        CastOffSuspend(s, 0, card);
        t_real_stut_node = nullptr; t_real_stut_state = nullptr; t_suspend_arrival_ctx = nullptr;
        REQUIRE(s.players[0].hand.size() == 1);
        CHECK(g_lp_node_calls == (mode == 0 ? 1 : 0));
        CHECK(s.players[0].hand[0].m_name.str() == cands[mode == 0 ? 1 : 0]);
    }
    // The node itself: one searched copy per rank, deterministic, nothing at depth 0 or with the lever off.
    GameState s = ProfaneDueState();
    const Card card = s.players[0].suspended_cards[0].card;
    s.players[0].suspended_cards[0].arrive_turn = s.TurnSeq();
    const std::vector<SuspendedCard> snap = s.players[0].suspended_cards;
    const std::vector<SuspendedCard> rem;
    s.players[0].suspended_cards.clear();
    const SuspendArrivalCtx ctx{ &snap, 0, &rem };
    const int width = SuspendTutorPickWidth(s, 0, pp);
    REQUIRE(width >= 2);
    {
        ArmScope n(heurarm::PROFANE_REAL_NODE, true);
        const int r = TurnSolver::ChooseRealResolutionFetch(s, 0, pp, card, ctx, 2, 8, nullptr, true);
        CHECK(r >= 0);
        CHECK(r < width);
        CHECK(TurnSolver::ChooseRealResolutionFetch(s, 0, pp, card, ctx, 2, 8, nullptr, true) == r);
        CHECK(TurnSolver::ChooseRealResolutionFetch(s, 0, pp, card, ctx, 0, 8, nullptr, true) == -1);
    }
    {
        ArmScope n(heurarm::PROFANE_REAL_NODE, false);
        CHECK(TurnSolver::ChooseRealResolutionFetch(s, 0, pp, card, ctx, 2, 8, nullptr, true) == -1);
    }
}
