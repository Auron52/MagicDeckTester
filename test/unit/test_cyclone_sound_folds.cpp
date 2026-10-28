// SOUND FOLDS of the cantrip search (docs/design/cantrip-search-optimization.md §2; Unpredictable Cyclone ledger,
// "Sound folds F2/F4/F5/F3 + Q36 (2026-10-28)").
//
//   * F2  MTG_PB_EXACT_WIDTH -- the Brainstorm / Brainsurge put-back fan is the resolution's EXACT candidate count
//         whenever nothing that can change the hand or the library runs before the put-back (a pure ritual ahead
//         of it makes the count a TIGHT upper bound: the ritual may still be dropped); otherwise the old bound.
#include <doctest/doctest.h>

#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HandEntry.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <map>
#include <string>
#include <vector>

namespace
{

void EnsureCardsSf()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefSf(const std::string& name)
{
    EnsureCardsSf();
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card CardSf(const std::string& name, int number)
{
    Card c     = DefSf(name).card;
    c.m_number = number;
    return c;
}

void PutSf(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = CardSf(name, number);
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

struct ArmScopeSf
{
    heurarm::Arm saved;
    ArmScopeSf(heurarm::Slot slot, int v) : saved(heurarm::t_arm) { heurarm::t_arm[static_cast<int>(slot)] = static_cast<std::int8_t>(v); }
    ~ArmScopeSf() { heurarm::t_arm = saved; }
};

// `islands` untapped Islands, Brainstorm + `hand_rest` in hand, `library` on top of six Ponders.
GameState BoardSf(int islands, bool drop_used, const std::vector<std::string>& hand_rest,
                  const std::vector<std::string>& library)
{
    EnsureCardsSf();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.game_seed           = 4242;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = drop_used ? 1 : 0;
    s.deck_has_draw_ping = false;   // the deck flags default ON (conservative); this board has neither
    s.deck_has_landfall  = false;
    for (int k = 0; k < islands; ++k) { PutSf(s, "Island", 1 + k); }
    s.players[0].hand.push_back(CardSf("Brainstorm", 20));
    int h = 21;
    for (const std::string& n : hand_rest) { s.players[0].hand.push_back(CardSf(n, h++)); }
    int l = 100;
    for (const std::string& n : library) { s.players[0].library.push_back(CardSf(n, l++)); }
    for (int k = 0; k < 6; ++k) { s.players[0].library.push_back(CardSf("Ponder", 200 + k)); }
    return s;
}

// The cast names of a plan, in order, plus its land: the identity a fan shares with its base.
std::string ShapeSf(const TurnSolver::Plan& p)
{
    std::string o = "land=" + p.land_to_play + ";";
    for (const Action& a : p.actions) { o += a.card_name.str() + ","; }
    return o;
}

// Fan sizes (base + its put-back variants) of every base plan of exactly `shape`, ignoring every other axis.
int FanSizeSf(const std::vector<TurnSolver::Plan>& plans, const std::string& shape)
{
    int group = 0;
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.putback_choice < 0 && p.putback_group != 0 && ShapeSf(p) == shape && p.scry_choice < 0
            && p.ponder_choice < 0 && p.bp_choice < 0)
        { group = p.putback_group; break; }
    }
    if (group == 0) { return 0; }
    int n = 0;
    for (const TurnSolver::Plan& p : plans) { if (p.putback_group == group) { ++n; } }
    return n;
}

// The resolution's candidate count, by hand: `hand` (after the casts and the land) + the top `draw` cards.
int ResolutionCountSf(const GameState& s, const std::vector<std::string>& leave, int draw)
{
    GameState probe = s;
    std::vector<Card>& h = probe.players[0].hand;
    for (const std::string& nm : leave)
    {
        for (auto it = h.begin(); it != h.end(); ++it) { if (it->m_name.str() == nm) { h.erase(it); break; } }
    }
    DrawIntoHand(probe, 0, draw, HandEntryReason::Draw);
    return static_cast<int>(PutBackCandidates(probe, 0, 2).size());
}

}   // namespace

TEST_CASE("Sound fold F2: the put-back fan is the resolution's exact count when nothing precedes it")
{
    // Hand after the draw: Island x2, Lightning Bolt x2, Birds, Sol Ring -> 4 classes, two doubled: 14 candidates.
    // The upper bound reads one trigger-slack card more (a Ponder): 5 classes -> 22.
    GameState s = BoardSf(1, true, { "Island", "Island", "Lightning Bolt" }, { "Lightning Bolt", "Birds of Paradise", "Sol Ring" });
    const int real = ResolutionCountSf(s, { "Brainstorm" }, 3);
    REQUIRE(real == 14);
    std::vector<TurnSolver::Plan> on, off;
    { ArmScopeSf _a(heurarm::PB_EXACT_WIDTH, 1); on  = TurnSolver::EnumerateMainPlans(s, true); }
    { ArmScopeSf _a(heurarm::PB_EXACT_WIDTH, 0); off = TurnSolver::EnumerateMainPlans(s, true); }
    const std::string shape = "land=;Brainstorm,";
    CHECK(FanSizeSf(on, shape) == real);
    CHECK(FanSizeSf(off, shape) > real);                 // the bound: clamped duplicates past the real list
    // Every rank of the exact fan names a DISTINCT put-back: apply each and compare the outcomes.
    std::map<std::string, int> outcomes;
    int group = 0;
    for (const TurnSolver::Plan& p : on) { if (p.putback_choice < 0 && ShapeSf(p) == shape && p.putback_group != 0) { group = p.putback_group; break; } }
    for (const TurnSolver::Plan& p : on)
    {
        if (p.putback_group != group) { continue; }
        GameState roll = s;
        TurnSolver::ApplyPlan(roll, p, true);
        std::string o = roll.players[0].library[0].m_name.str() + "/" + roll.players[0].library[1].m_name.str();
        ++outcomes[o];
    }
    CHECK(outcomes.size() == static_cast<std::size_t>(real));
}

TEST_CASE("Sound fold F2: a land-first inert land leaves the hand first; a fetchland or a cantrip ahead keeps the bound")
{
    // Land drop open, an Island in hand (inert) and a Scalding Tarn (a fetch: shuffles before the draw).
    GameState s = BoardSf(1, false, { "Island", "Lightning Bolt", "Scalding Tarn" },
                          { "Lightning Bolt", "Birds of Paradise", "Sol Ring" });
    std::vector<TurnSolver::Plan> on, off;
    { ArmScopeSf _a(heurarm::PB_EXACT_WIDTH, 1); on  = TurnSolver::EnumerateMainPlans(s, true); }
    { ArmScopeSf _a(heurarm::PB_EXACT_WIDTH, 0); off = TurnSolver::EnumerateMainPlans(s, true); }
    // land=Island: the hand at the put-back is Bolt x2, Tarn, Birds, Sol Ring (4 classes, one doubled) -> 13.
    const int real_island = ResolutionCountSf(s, { "Brainstorm", "Island" }, 3);
    CHECK(real_island == 13);
    CHECK(FanSizeSf(on, "land=Island;Brainstorm,") == real_island);
    CHECK(FanSizeSf(off, "land=Island;Brainstorm,") > real_island);
    // land=Scalding Tarn: a shuffle before the draw -- the old bound, exactly as with the fold off.
    const int tarn_on  = FanSizeSf(on, "land=Scalding Tarn;Brainstorm,");
    const int tarn_off = FanSizeSf(off, "land=Scalding Tarn;Brainstorm,");
    CHECK(tarn_on > 0);
    CHECK(tarn_on == tarn_off);
}

TEST_CASE("Sound fold F2: a cantrip ahead of the put-back keeps the upper bound")
{
    // Preordain (scry 2, draw 1) ahead of Brainstorm: the drawn cards are not known -- the bound stays.
    GameState s = BoardSf(2, true, { "Preordain", "Goblin Guide" },
                          { "Lightning Bolt", "Birds of Paradise", "Sol Ring", "Llanowar Elves", "Izzet Signet" });
    std::vector<TurnSolver::Plan> on, off;
    { ArmScopeSf _a(heurarm::PB_EXACT_WIDTH, 1); on  = TurnSolver::EnumerateMainPlans(s, true); }
    { ArmScopeSf _a(heurarm::PB_EXACT_WIDTH, 0); off = TurnSolver::EnumerateMainPlans(s, true); }
    std::map<std::string, int> fan_on, fan_off;
    for (const TurnSolver::Plan& p : on)  { if (p.putback_group != 0) { ++fan_on[ShapeSf(p)]; } }
    for (const TurnSolver::Plan& p : off) { if (p.putback_group != 0) { ++fan_off[ShapeSf(p)]; } }
    bool seen_before = false, seen_after = false;
    for (const auto& [shape, n] : fan_off)
    {
        const std::size_t pre = shape.find("Preordain"), bs = shape.find("Brainstorm");
        if (pre == std::string::npos || bs == std::string::npos) { continue; }
        INFO(shape);
        if (pre < bs)
        {
            seen_before = true;
            CHECK(fan_on[shape] == n);   // the Preordain resolves first: the bound stays
        }
        else
        {
            seen_after = true;
            CHECK(fan_on[shape] < n);    // F2's sharpening: a cantrip AFTER the put-back does not unsettle it
        }
    }
    CHECK((seen_before || seen_after));
}

TEST_CASE("Sound fold F2: a ritual ahead of the put-back counts as still in hand (a tight upper bound)")
{
    // Seething Song ({2}{R}: add RRRRR) then Brainstorm, three Mountains. The ritual is counted as still in hand
    // (the apply may drop it): the fan is the count WITH the Song, which bounds the real list from above.
    GameState s = BoardSf(0, true, { "Seething Song", "Lightning Bolt" }, { "Lightning Bolt", "Birds of Paradise", "Sol Ring" });
    for (int k = 0; k < 3; ++k) { PutSf(s, "Mountain", 60 + k); }
    PutSf(s, "Island", 70);
    std::vector<TurnSolver::Plan> on;
    { ArmScopeSf _a(heurarm::PB_EXACT_WIDTH, 1); on = TurnSolver::EnumerateMainPlans(s, true); }
    const int with_song    = ResolutionCountSf(s, { "Brainstorm" }, 3);
    const int without_song = ResolutionCountSf(s, { "Brainstorm", "Seething Song" }, 3);
    REQUIRE(with_song > without_song);
    int fan = 0;
    for (const TurnSolver::Plan& p : on)
    {
        if (p.putback_choice < 0 && p.putback_group != 0 && p.land_to_play.empty() && p.actions.size() == 2
            && p.actions[0].card_name.str() == "Seething Song" && p.actions[1].card_name.str() == "Brainstorm")
        {
            for (const TurnSolver::Plan& q : on) { if (q.putback_group == p.putback_group) { ++fan; } }
            break;
        }
    }
    if (fan > 0) { CHECK(fan == with_song); }
}
