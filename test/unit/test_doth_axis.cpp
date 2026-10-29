// THE SEARCHED DEFENSE OF THE HEART PUT LIST (MTG_DOTH_AXIS, DEFAULT ON; MTG_DOTH_FOLDS, DEFAULT ON).
//
// Defense of the Heart's upkeep trigger ("search your library for up to two creature cards, put those cards onto
// the battlefield") used to take WHICH creatures and their ENTER ORDER from the provider's closed-form pick
// (SacTutorPutList) at the executor's upkeep and at every simulated turn boundary the search crossed with depth
// left -- an in-window greedy pick. Now the search crosses the boundary once per entry of the provider's menu
// (SacTutorPutOptions, the provider's own pick first) and the plan of the turn that crossed carries the chosen
// rank (Plan::doth_put_choice -> GameState::scripted_doth_put). These pin:
//   * the pin encoding;
//   * the menu: the provider's pick at rank 0, Phantasm/Wurm in both orders, the proven folds (a commuting order
//     kept once, no free slot while a free-gain card is spare), and the fold's soundness on a real crossing;
//   * the rollout's upkeep and the executor's upkeep (GameEngine) resolve the same rank, and both consume it;
//   * the boundary fan crosses every entry on a real Defense of the Heart board (an odometer over two triggers),
//     the provider's pick first, and nothing with the lever off or a covering pin;
//   * the full-line search finds the two-trigger upkeep kill the closed-form pick misses, and records it on the
//     main-1 plan -- and does NOT with MTG_DOTH_AXIS=0.
// Zone cards are NAME-ONLY placeholders, the way real play builds them (DeckLoader::MakePlaceholder).
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameEngine.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{

void EnsureCardsDh()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CreatureGivingProvider& CgProvDh()
{
    static const CreatureGivingProvider p;
    return p;
}

Card PhDh(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

void PutDh(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = CardDatabase::Instance().Lookup(name)->card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

// A vanilla 1/1 the opponent controls (the Orchard Spirit shape, without the turn-start spawn).
void OppTokenDh(GameState& s, int number)
{
    Card token;
    token.m_name = "1/1 Creature";
    token.RehashName();
    token.AddType(CardType::Creature);
    token.m_power     = 1;
    token.m_toughness = 1;
    token.m_number    = number;
    Permanent p;
    p.card             = token;
    p.controller_index = 1;
    p.owner_index      = 1;
    p.is_token         = true;
    s.battlefield.push_back(p);
}

// End of turn 3 (the boundary into turn 4's upkeep): one Suture Priest, `n_doth` Defense of the Heart, three
// opposing 1/1s; the library holds two Hunted Phantasms, a Massacre Wurm and a Suture Priest above Forests (so
// the draw is a land). The Creature Giving provider owns the menu.
GameState BoardDh(int n_doth, int opp_life)
{
    EnsureCardsDh();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.players[0].life     = 20;
    s.players[1].life     = opp_life;
    s.m_provider          = &CgProvDh();
    PutDh(s, "Forest", 1);
    PutDh(s, "Forest", 2);
    PutDh(s, "Suture Priest", 10);
    for (int k = 0; k < n_doth; ++k) { PutDh(s, "Defense of the Heart", 11 + k); }
    for (int k = 0; k < 3; ++k) { OppTokenDh(s, 900 + k); }
    s.players[0].library.push_back(PhDh("Hunted Phantasm", 40));
    s.players[0].library.push_back(PhDh("Hunted Phantasm", 41));
    s.players[0].library.push_back(PhDh("Massacre Wurm", 42));
    s.players[0].library.push_back(PhDh("Suture Priest", 43));
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(PhDh("Forest", 50 + k)); }
    return s;
}

std::multiset<std::string> OwnCreatures(const GameState& s)
{
    std::multiset<std::string> out;
    for (const Permanent& p : s.battlefield)
    { if (p.controller_index == 0 && p.card.IsCreature()) { out.insert(p.card.m_name.str()); } }
    return out;
}

std::multiset<std::string> Puts(const GameState& before, const GameState& after)
{
    std::multiset<std::string> b = OwnCreatures(before), a = OwnCreatures(after);
    for (const std::string& n : b) { const auto it = a.find(n); if (it != a.end()) { a.erase(it); } }
    return a;
}

std::multiset<std::string> AsSet(const std::vector<std::string>& v) { return { v.begin(), v.end() }; }

const CardParams& DothParams()
{
    return CardDatabase::Instance().Lookup("Defense of the Heart")->params;
}

int RankOf(const std::vector<std::vector<std::string>>& menu, const std::vector<std::string>& lst)
{
    for (std::size_t i = 0; i < menu.size(); ++i) { if (menu[i] == lst) { return static_cast<int>(i); } }
    return -1;
}

struct ArmDh
{
    int slot;
    ArmDh(int sl, int v) : slot(sl) { heurarm::t_arm[slot] = static_cast<std::int8_t>(v); }
    ~ArmDh() { heurarm::t_arm[slot] = -1; }
};

}   // namespace

TEST_CASE("DotH pin: the packed ranks and cover")
{
    CHECK(DothPinCover(-1) == 0);
    CHECK(DothPinRank(-1, 0) == 0);
    const int r[2] = { 5, 17 };
    const int pin = DothPinPack(2, r);
    CHECK(pin >= 0);
    CHECK(DothPinCover(pin) == 2);
    CHECK(DothPinRank(pin, 0) == 5);
    CHECK(DothPinRank(pin, 1) == 17);
    CHECK(DothPinRank(pin, 2) == 0);   // past the cover: the provider's pick
    const int r4[4] = { 127, 0, 64, 3 };
    const int p4 = DothPinPack(4, r4);
    CHECK(p4 >= 0);
    CHECK(DothPinCover(p4) == 4);
    CHECK(DothPinRank(p4, 0) == 127);
    CHECK(DothPinRank(p4, 2) == 64);
    CHECK(DothPinRank(p4, 3) == 3);
}

TEST_CASE("DotH menu: the provider's pick first, both Phantasm/Wurm orders, the proven folds")
{
    GameState s = BoardDh(1, 40);
    // A wider library: every proven-pool name, two copies of the free-gain ones.
    for (const char* nm : { "Soul Warden", "Essence Warden", "Birds of Paradise", "Varchild's War-Riders" })
    {
        s.players[0].library.push_back(PhDh(nm, 70 + static_cast<int>(s.players[0].library.size())));
        s.players[0].library.push_back(PhDh(nm, 70 + static_cast<int>(s.players[0].library.size())));
    }
    const auto menu = CgProvDh().SacTutorPutOptions(s, 0, DothParams(), 2);
    REQUIRE(menu.size() > 1);
    // Rank 0 IS the closed-form pick (Phantasm then Wurm: 5 gifts x 1 Priest + 8 deaths x 2).
    CHECK(menu[0] == CgProvDh().SacTutorPutList(s, 0, DothParams(), 2));
    CHECK(menu[0] == std::vector<std::string>{ "Hunted Phantasm", "Massacre Wurm" });
    // Both orders of the one pair whose order matters.
    CHECK(RankOf(menu, { "Massacre Wurm", "Hunted Phantasm" }) > 0);
    // No duplicate entries, a commuting pair kept ONCE, no free slot while a free-gain card is spare.
    std::set<std::vector<std::string>> seen(menu.begin(), menu.end());
    CHECK(seen.size() == menu.size());
    CHECK((RankOf(menu, { "Suture Priest", "Hunted Phantasm" }) >= 0)
          != (RankOf(menu, { "Hunted Phantasm", "Suture Priest" }) >= 0));
    for (std::size_t i = 1; i < menu.size(); ++i) { CHECK(menu[i].size() == 2); }
    // 7 names: 28 unordered pairs (every name has >= 2 copies but Wurm and Priest: 26) + the Wurm-first order.
    CHECK(menu.size() == 27);

    // MTG_DOTH_FOLDS=0: the unfolded menu -- both orders of a commuting pair, the singles and the empty list.
    ArmDh off(heurarm::DOTH_FOLDS, 0);
    const auto raw = CgProvDh().SacTutorPutOptions(s, 0, DothParams(), 2);
    CHECK(raw.size() > menu.size());
    CHECK(raw[0] == menu[0]);
    CHECK(RankOf(raw, { "Suture Priest", "Hunted Phantasm" }) > 0);
    CHECK(RankOf(raw, { "Hunted Phantasm", "Suture Priest" }) > 0);
    CHECK(RankOf(raw, { "Soul Warden" }) > 0);
    CHECK(RankOf(raw, {}) > 0);
}

TEST_CASE("DotH menu: a creature outside the proven pool gets no fold")
{
    GameState s = BoardDh(1, 40);
    s.players[0].library.push_back(PhDh("Llanowar Elves", 80));
    const auto menu = CgProvDh().SacTutorPutOptions(s, 0, DothParams(), 2);
    // Unproven: both orders with every other name, and no dominance over its free slot.
    CHECK(RankOf(menu, { "Llanowar Elves", "Suture Priest" }) >= 0);
    CHECK(RankOf(menu, { "Suture Priest", "Llanowar Elves" }) >= 0);
}

TEST_CASE("DotH fold soundness: a commuting order reaches the same state; Phantasm/Wurm does not")
{
    ArmDh unfold(heurarm::DOTH_FOLDS, 0);
    const GameState s = BoardDh(1, 40);
    const auto raw = CgProvDh().SacTutorPutOptions(s, 0, DothParams(), 2);
    auto cross = [&](const std::vector<std::string>& lst)
    {
        const int r = RankOf(raw, lst);
        REQUIRE(r >= 0);
        GameState g = s;
        const int rr[1] = { r };
        g.scripted_doth_put = DothPinPack(1, rr);
        REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(g));
        return g;
    };
    auto same = [](const GameState& a, const GameState& b)
    {
        std::multiset<std::string> ba, bb;
        for (const Permanent& p : a.battlefield) { ba.insert(std::to_string(p.controller_index) + p.card.m_name.str()); }
        for (const Permanent& p : b.battlefield) { bb.insert(std::to_string(p.controller_index) + p.card.m_name.str()); }
        return ba == bb && a.players[0].life == b.players[0].life && a.players[1].life == b.players[1].life
            && a.players[0].library.size() == b.players[0].library.size()
            && a.players[0].hand.size() == b.players[0].hand.size();
    };
    CHECK(same(cross({ "Suture Priest", "Hunted Phantasm" }), cross({ "Hunted Phantasm", "Suture Priest" })));
    CHECK(same(cross({ "Suture Priest", "Massacre Wurm" }), cross({ "Massacre Wurm", "Suture Priest" })));
    CHECK_FALSE(same(cross({ "Hunted Phantasm", "Massacre Wurm" }), cross({ "Massacre Wurm", "Hunted Phantasm" })));
}

TEST_CASE("DotH: the rollout's upkeep and the executor's upkeep resolve the same rank, and both consume it")
{
    const GameState board = BoardDh(1, 40);
    const auto menu = CgProvDh().SacTutorPutOptions(board, 0, DothParams(), 2);
    REQUIRE(menu.size() >= 3);
    for (const int r : { 0, 1, static_cast<int>(menu.size()) - 1 })
    {
        const int rr[1] = { r };
        // The rollout: the turn boundary itself.
        GameState ro = board;
        ro.scripted_doth_put = DothPinPack(1, rr);
        REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(ro));
        CHECK(ro.scripted_doth_put == -1);
        CHECK(Puts(board, ro) == AsSet(menu[static_cast<std::size_t>(r)]));
        // The executor: GameEngine's real upkeep (resumed at the upkeep tail of turn 4, the turn played out).
        GameState ex = board;
        ex.turn_number       = 4;
        ex.scripted_doth_put = DothPinPack(1, rr);
        AIEngine eng(MulliganProfile::DefaultProfile(), 0, 0);
        GameEngine ge(eng);
        ge.PlayOutFrom(ex, ex.turn_number, GameEngine::ResumeAt::UpkeepTail);
        CHECK(ex.scripted_doth_put == -1);
        CHECK(Puts(board, ex) == AsSet(menu[static_cast<std::size_t>(r)]));
    }
    // Unpinned: the provider's pick (rank 0) in both worlds.
    GameState ro = board;
    REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(ro));
    CHECK(Puts(board, ro) == AsSet(menu[0]));
}

TEST_CASE("DotH boundary fan: every menu entry is crossed, the provider's pick first; nothing off or covered")
{
    ArmDh on(heurarm::DOTH_AXIS, 1);
    const GameState board = BoardDh(1, 40);
    const auto menu = CgProvDh().SacTutorPutOptions(board, 0, DothParams(), 2);
    const std::vector<int> pins = TurnSolver::DothBoundaryPinsForTest(board);
    REQUIRE(pins.size() == menu.size());
    for (std::size_t i = 0; i < pins.size(); ++i)
    {
        REQUIRE(pins[i] >= 0);
        CHECK(DothPinCover(pins[i]) == 1);
        CHECK(DothPinRank(pins[i], 0) == static_cast<int>(i));
    }
    // A pin already covering the trigger (an earlier plan decided it): one crossing, nothing decided.
    GameState covered = board;
    const int r2[1] = { 2 };
    covered.scripted_doth_put = DothPinPack(1, r2);
    CHECK(TurnSolver::DothBoundaryPinsForTest(covered) == std::vector<int>{ -1 });
    // MTG_DOTH_AXIS=0: one crossing, the provider's pick, nothing recorded.
    ArmDh off(heurarm::DOTH_AXIS, 0);
    CHECK(TurnSolver::DothBoundaryPinsForTest(board) == std::vector<int>{ -1 });
}

TEST_CASE("DotH boundary fan: two triggers in one upkeep are an odometer over both ordinals")
{
    ArmDh on(heurarm::DOTH_AXIS, 1);
    const GameState board = BoardDh(2, 40);
    const std::vector<int> pins = TurnSolver::DothBoundaryPinsForTest(board);
    REQUIRE(pins.size() > 1);
    CHECK(pins[0] >= 0);
    CHECK(DothPinRank(pins[0], 0) == 0);   // the provider's picks first
    std::set<int> ord0_ranks, two_trigger_ord1;
    for (const int p : pins)
    {
        REQUIRE(p >= 0);
        ord0_ranks.insert(DothPinRank(p, 0));
        if (DothPinCover(p) == 2) { two_trigger_ord1.insert(DothPinRank(p, 1)); }
    }
    // The closed-form pick sweeps the board, so the second trigger's intervening if fails (one ordinal); a
    // non-sweeping first put leaves it live, and then the second trigger's menu is walked too.
    CHECK(DothPinCover(pins[0]) == 1);
    CHECK(ord0_ranks.size() > 1);
    CHECK(two_trigger_ord1.size() > 1);
    std::set<int> uniq(pins.begin(), pins.end());
    CHECK(uniq.size() == pins.size());
}

TEST_CASE("DotH search: the fan finds the two-trigger upkeep kill the closed-form pick misses")
{
    // Turn 3's main phase, empty hand, two Defense of the Heart, opponent on 40. The closed-form pick for the first
    // trigger (Phantasm then Wurm: 21) sweeps the board, so the second trigger's intervening if fails and the
    // opponent survives on ~18. Putting Priest + Phantasm first (10, eight opposing creatures) lets the second
    // trigger put Phantasm then Wurm (10 + 13 deaths x 2 = 36): dead in turn 4's upkeep.
    auto search = [](int axis) -> TurnSolver::SearchLine
    {
        ArmDh arm(heurarm::DOTH_AXIS, axis);
        GameState s = BoardDh(2, 40);
        s.phase = Phase::PreCombatMain;
        return TurnSolver::FullSearchLine(s, 2, 8, /*second_main=*/false, nullptr, nullptr);
    };
    const TurnSolver::SearchLine on = search(1);
    CHECK(on.win_turn == 4);
    REQUIRE_FALSE(on.phases.empty());
    const int pin = on.phases.front().plan.doth_put_choice;
    REQUIRE(pin >= 0);
    CHECK(DothPinCover(pin) == 2);              // both triggers decided on turn 3's plan
    CHECK(DothPinRank(pin, 0) > 0);             // not the provider's pick
    // The plan's apply writes it for turn 4's upkeep.
    GameState applied = BoardDh(2, 40);
    TurnSolver::ApplyPlan(applied, on.phases.front().plan, true);
    CHECK(applied.scripted_doth_put == pin);

    const TurnSolver::SearchLine off = search(0);
    CHECK(off.win_turn > 4);
    if (!off.phases.empty()) { CHECK(off.phases.front().plan.doth_put_choice == -1); }
}
