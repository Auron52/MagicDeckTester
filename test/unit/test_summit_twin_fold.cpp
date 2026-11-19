// The SUMMIT-TWIN FOLD (2026-11-19; TurnSolver.cpp SummitTwinFold, MTG_SUMMIT_TWIN_FOLD, DEFAULT ON). Guild Summit's
// "tap any number of untapped Gates you control; draw a card for each" is searched on the ETB choice axis as the
// unpinned cast (the count decided at resolution: Maze's End's rest-of-line count) plus pinned clones (chosen_x =
// N + 1; Maze's End: N = 0 and N = all). A pinned clone whose count, clamped at resolution, equals the unpinned
// twin's count is the same choice: the same Gates tapped (EtbTapSubtypeOrder decides WHICH from the count alone),
// the same cards drawn, the same state. The fold declines it before its unit charge. These tests hold:
//   1. the resolution's witness (g_summit_tap_*) and the interchangeability premise -- one count, one tapped set;
//   2. the identity, plan by plan: every twin whose count lands on its base's reaches its base's exact state, and
//      a twin whose count differs does NOT (it is a different choice, never folded);
//   3. the search: the same committed line and win turn with the fold on and off, at an unlimited budget; the fold
//      declines the "tap all" clone where the rest-of-line count is all.
//   (2 also holds the search apply to the PIN: before 2026-11-19 TurnSolver's noncreature ETB path dropped it.)
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
#include <sstream>
#include <string>
#include <vector>

namespace
{
void LoadStfCards()
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& D(const char* name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    return *d;
}

Card C(const char* name, int number)
{
    Card c = D(name).card;
    c.m_number = number;
    return c;
}

void Put(GameState& s, const char* name, int number, bool tapped = false)
{
    Permanent p;
    p.card = C(name, number);
    p.controller_index = 0;
    p.owner_index = 0;
    p.tapped = tapped;
    s.battlefield.push_back(p);
}

struct Arm
{
    int slot;
    Arm(int sl, int v) : slot(sl) { heurarm::t_arm[sl] = static_cast<std::int8_t>(v); }
    ~Arm() { heurarm::t_arm[slot] = -1; }
};

// Four untapped Gates (Baldur's Gate among them: EtbTapSubtypeOrder taps it last), Guild Summit and a Gate in hand.
// With `maze` an untapped Maze's End reserves its {3}, so the rest-of-line count is below all.
GameState Board(const DecisionProvider& prov, bool maze)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.m_provider          = &prov;
    Put(s, "Azorius Guildgate", 1);
    Put(s, "Dimir Guildgate", 2);
    Put(s, "Simic Guildgate", 3);
    Put(s, "Izzet Guildgate", 4);
    Put(s, "Baldur's Gate", 5);
    if (maze) { Put(s, "Maze's End", 6); }
    s.players[0].hand.push_back(C("Guild Summit", 10));
    s.players[0].hand.push_back(C("Golgari Guildgate", 11));
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(C(k % 2 ? "Forest" : "Island", 30 + k)); }
    return s;
}

// The plan's one Guild Summit cast, or nullptr.
const Action* SummitCast(const TurnSolver::Plan& p)
{
    const Action* out = nullptr;
    for (const Action& a : p.actions)
    {
        if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Guild Summit")
        {
            if (out != nullptr) { return nullptr; }
            out = &a;
        }
    }
    return out;
}

// Everything a Summit resolution or the rest of the apply could move.
std::string Digest(const GameState& s)
{
    std::ostringstream o;
    for (const Permanent& p : s.battlefield)
    { o << p.card.m_number << (p.tapped ? "t" : "u") << p.controller_index << ","; }
    o << "|h";
    for (const Card& c : s.players[0].hand) { o << c.m_number << ","; }
    o << "|l";
    for (const Card& c : s.players[0].library) { o << c.m_number << ","; }
    o << "|g";
    for (const Card& c : s.players[0].graveyard) { o << c.m_number << ","; }
    o << "|f" << s.floating_mana.Total() << "|lp" << s.players[0].lands_played_this_turn
      << "|d" << s.players[0].cards_drawn_this_turn << "|life" << s.players[0].life << "/" << s.players[1].life;
    return o.str();
}

std::set<int> TappedGates(const GameState& s)
{
    std::set<int> out;
    for (const Permanent& p : s.battlefield)
    {
        if (p.tapped && p.card.m_number >= 1 && p.card.m_number <= 5) { out.insert(p.card.m_number); }
    }
    return out;
}
}   // namespace

TEST_CASE("Summit-twin fold: the resolution's witness, and one count taps one set of Gates")
{
    LoadStfCards();
    const MazesEndProvider prov;
    const CardDefinition& summit = D("Guild Summit");
    // A plan-free context (another test may leave the thread's line hold set; every real apply binds its own).
    const LineUnpaidCostScope clean(ManaCost{});
    for (bool maze : { false, true })
    {
        GameState s = Board(prov, maze);
        s.players[0].hand.erase(s.players[0].hand.begin());   // Summit goes straight onto the battlefield
        Put(s, "Guild Summit", 20);
        const int idx = static_cast<int>(s.battlefield.size()) - 1;
        // Unpinned: the provider's rest-of-line count (all five Gates; fewer with an untapped Maze's End).
        GameState u = s;
        const unsigned long long r0 = g_summit_tap_resolutions;
        FireOwnEtbTriggers(u, 0, idx, std::string(), /*etb_kx=*/0);
        CHECK(g_summit_tap_resolutions == r0 + 1);
        CHECK(g_summit_tap_last_avail == 5);
        CHECK_FALSE(g_summit_tap_last_pinned);
        const int n = g_summit_tap_last_n;
        CHECK(n >= 0);
        CHECK(n <= 5);
        if (!maze) { CHECK(n == 5); }   // nothing to reserve mana for: every Gate
        CHECK(u.players[0].hand.size() == static_cast<std::size_t>(1 + n));   // + the Golgari Guildgate
        // Every pinned count: the pin decides (clamped to the Gates present), and a pin equal to the unpinned
        // count taps EXACTLY the unpinned Gates and draws exactly its cards -- the interchangeability premise.
        for (int pin = 0; pin <= 7; ++pin)
        {
            GameState t = s;
            FireOwnEtbTriggers(t, 0, idx, std::string(), /*etb_kx=*/pin + 1);
            CHECK(g_summit_tap_last_pinned);
            CHECK(g_summit_tap_last_avail == 5);
            const int got = g_summit_tap_last_n;
            CHECK(got == std::clamp(pin, 0, 5));
            CHECK(static_cast<int>(TappedGates(t).size()) == got);
            if (got == n) { CHECK(Digest(t) == Digest(u)); }
            else          { CHECK(Digest(t) != Digest(u)); }
        }
        // WHICH Gates is not free: Baldur's Gate taps LAST (EtbTapSubtypeOrder), so a count of 4 leaves it up.
        GameState four = s;
        FireOwnEtbTriggers(four, 0, idx, std::string(), /*etb_kx=*/5);
        CHECK(TappedGates(four) == std::set<int>{ 1, 2, 3, 4 });
    }
}

TEST_CASE("Summit-twin fold: a twin whose count lands on its base's IS its base; a different count is not")
{
    LoadStfCards();
    const MazesEndProvider prov;
    for (bool maze : { false, true })
    {
        const GameState s = Board(prov, maze);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
        // Apply every plan carrying one Summit cast, keyed by the plan with that cast's chosen_x masked.
        struct Res { std::string digest; int avail = -1; int n = -1; bool pinned = false; int pin = -1; };
        std::vector<std::pair<TurnSolver::Plan, Res>> applied;
        for (const TurnSolver::Plan& p : plans)
        {
            const Action* sa = SummitCast(p);
            if (sa == nullptr) { continue; }
            GameState t = s;
            const unsigned long long r0 = g_summit_tap_resolutions;
            TurnSolver::ApplyPlan(t, p, /*is_pre_combat=*/true);
            if (g_summit_tap_resolutions != r0 + 1) { continue; }   // the Summit never resolved (or twice)
            Res r;
            r.digest = Digest(t);
            r.avail = g_summit_tap_last_avail;
            r.n = g_summit_tap_last_n;
            r.pinned = g_summit_tap_last_pinned;
            r.pin = sa->chosen_x - 1;
            CHECK(r.pinned == (sa->chosen_x >= 1));
            TurnSolver::Plan masked = p;
            masked.actions[static_cast<std::size_t>(sa - p.actions.data())].chosen_x = 0;
            applied.emplace_back(masked, r);
        }
        int same = 0, differ = 0;
        for (std::size_t i = 0; i < applied.size(); ++i)
        {
            if (!applied[i].second.pinned) { continue; }
            for (std::size_t j = 0; j < applied.size(); ++j)
            {
                if (applied[j].second.pinned || !(applied[j].first == applied[i].first)) { continue; }
                const Res& base = applied[j].second;
                const Res& twin = applied[i].second;
                CHECK(twin.avail == base.avail);   // the same state at the resolution
                if (std::clamp(twin.pin, 0, base.avail) == base.n) { CHECK(twin.digest == base.digest); ++same; }
                else                                              { CHECK(twin.digest != base.digest); ++differ; }
            }
        }
        // The board has both kinds: the fold's twins and genuinely different counts.
        CHECK(same > 0);
        CHECK(differ > 0);
    }
}

TEST_CASE("Summit-twin fold: the search commits the same line with the fold on and off, and the fold fires")
{
    LoadStfCards();
    const MazesEndProvider prov;
    for (bool maze : { false, true })
    {
        const GameState s = Board(prov, maze);
        int win_off = -1, sd_off = -1, win_on = -1, sd_on = -1;
        TurnSolver::Plan off_plan;
        const long long f0 = TurnSolver::SummitTwinFoldsForTest();
        {
            const Arm off(heurarm::SUMMIT_TWIN_FOLD, 0);
            off_plan = TurnSolver::SolveWithLookahead(s, /*is_pre_combat=*/true, /*depth=*/2, /*max_turns=*/8,
                                                      /*budget=*/nullptr, /*enforce_budget=*/false,
                                                      /*second_main=*/false, nullptr, &win_off, &sd_off);
        }
        const long long f1 = TurnSolver::SummitTwinFoldsForTest();
        CHECK(f1 == f0);   // off: nothing declined
        const TurnSolver::Plan on_plan = TurnSolver::SolveWithLookahead(s, true, 2, 8, nullptr, false, false,
                                                                        nullptr, &win_on, &sd_on);
        const long long f2 = TurnSolver::SummitTwinFoldsForTest();
        // on: without a Maze's End the rest-of-line count is every Gate, so the "tap all" clone lands on its unpinned
        // twin and is declined (with one, whether a pin lands on the count depends on the line; nothing is required).
        if (!maze) { CHECK(f2 > f1); }
        CHECK(win_on == win_off);
        CHECK(sd_on == sd_off);
        CHECK(on_plan.land_to_play == off_plan.land_to_play);
        REQUIRE(on_plan.actions.size() == off_plan.actions.size());
        for (std::size_t k = 0; k < on_plan.actions.size(); ++k)
        {
            CHECK(on_plan.actions[k].card_name == off_plan.actions[k].card_name);
            CHECK(on_plan.actions[k].chosen_x == off_plan.actions[k].chosen_x);
        }
        // ...and the committed line plays to the same state.
        GameState a = s, b = s;
        TurnSolver::ApplyPlan(a, on_plan, true);
        TurnSolver::ApplyPlan(b, off_plan, true);
        CHECK(Digest(a) == Digest(b));
    }
}
