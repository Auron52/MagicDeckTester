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
#include "ai/SearchBudget.h"

#include <algorithm>

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

// ======================================================================================================================
//   * F3  MTG_PB_MIRROR_FOLD -- when the put-back's next library event is a shuffle blind to the swap, (a, b) and (b, a)
//         reach one state (ShuffleByKey keys on card numbers): the mirror of an applied rank is declined.
//   * F4  MTG_PB_STRAND_FOLD (DEFAULT OFF, measured not exact) -- a rank that puts back the last copy of a card cast
//         later realises the smaller plan's same-pair member, up to the payment allocation.
//   * F5  MTG_PB_XBASE_FOLD -- at a breakpoint-node host, a base whose prefix collapsed onto an earlier base's has
//         every rank collapse there too: declined unbuilt.
//   * Q36 MTG_CYCLE_SEED_WALK_FOLD -- a seed-only plan with Cyclone out: seed variants whose scry + walk give the same
//         (hit, library after, bottomed set) as an earlier sibling with the same free-cast pin are not emitted.
namespace
{
// A plan's OUTCOME by card number: the whole library order, the hand, the battlefield names (taps ignored).
std::string OutcomeSf(const GameState& r)
{
    std::string o = "lib=";
    for (std::size_t i = 0; i < r.players[0].library.size(); ++i) { o += std::to_string(r.players[0].library[i].m_number) + ","; }
    std::vector<int> h;
    for (const Card& c : r.players[0].hand) { h.push_back(c.m_number); }
    std::sort(h.begin(), h.end());
    o += " hand=";
    for (int n : h) { o += std::to_string(n) + ","; }
    std::vector<std::string> bf;
    for (const Permanent& p : r.battlefield) { bf.push_back(p.card.m_name.str()); }
    std::sort(bf.begin(), bf.end());
    o += " bf=";
    for (const std::string& n : bf) { o += n + ","; }
    return o;
}
long long FoldCounterSf(int which)
{
    long long c[5];
    TurnSolver::SoundFoldCounters(c);
    return c[which];
}
// Every member of the fan of the first base of `shape`, applied: its outcomes (with multiplicity).
std::map<std::string, int> FanOutcomesSf(const GameState& s, const std::vector<TurnSolver::Plan>& plans, const std::string& shape,
                                         int* fan_size = nullptr)
{
    int group = 0;
    for (const TurnSolver::Plan& p : plans)
    { if (p.putback_choice < 0 && p.putback_group != 0 && ShapeSf(p) == shape && p.bp_choice < 0) { group = p.putback_group; break; } }
    std::map<std::string, int> out;
    int n = 0;
    for (const TurnSolver::Plan& p : plans)
    {
        if (group == 0 || p.putback_group != group) { continue; }
        ++n;
        GameState r = s;
        TurnSolver::ApplyPlan(r, p, true);
        ++out[OutcomeSf(r)];
    }
    if (fan_size != nullptr) { *fan_size = n; }
    return out;
}
}   // namespace

TEST_CASE("Sound fold F3: a put-back then a land-search shuffle -- mirrors reach one state, and the fold declines them")
{
    // Three Islands and an untapped Myriad Landscape: Brainstorm ({U}), then Myriad ({2}, {T}, sacrifice: two basics
    // sharing a type, tapped; then SHUFFLE). The put-back hand holds no land, so the land search cannot see the swap.
    GameState s = BoardSf(3, true, { "Lightning Bolt", "Goblin Guide" },
                          { "Lightning Bolt", "Birds of Paradise", "Sol Ring", "Island", "Island", "Mountain" });
    PutSf(s, "Myriad Landscape", 9);
    s.deck_has_land_search = true;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    int fan = 0, fan_plain = 0;
    const std::map<std::string, int> with_shuffle = FanOutcomesSf(s, plans, "land=;Brainstorm,Myriad Landscape,", &fan);
    const std::map<std::string, int> plain        = FanOutcomesSf(s, plans, "land=;Brainstorm,", &fan_plain);
    // Hand at the put-back: Bolt x2, Goblin Guide, Birds, Sol Ring -> 4 classes, one doubled: 13 ordered put-backs.
    REQUIRE(fan == 13);
    REQUIRE(fan_plain == 13);
    CHECK(plain.size() == 13u);             // no shuffle: every ordered put-back is its own state
    CHECK(with_shuffle.size() == 7u);       // the shuffle folds each (a, b) onto (b, a): 4*3/2 + 1
    for (const auto& [o, k] : with_shuffle) { CHECK((k == 2 || k == 1)); }
    // THE FOLD: a lookahead solve declines the mirrors (counted), and at an unlimited budget commits the same outcome.
    auto solve = [&](int on) -> std::string
    {
        ArmScopeSf _a(heurarm::PB_MIRROR_FOLD, on);
        const TurnSolver::Plan p = TurnSolver::SolveWithLookahead(s, true, 2, 8, nullptr, false, false, nullptr);
        GameState r = s;
        TurnSolver::ApplyPlan(r, p, true);
        return OutcomeSf(r);
    };
    const long long f0 = FoldCounterSf(1);
    const std::string on = solve(1);
    const long long f1 = FoldCounterSf(1);
    const std::string off = solve(0);
    CHECK(FoldCounterSf(1) == f1);          // off: nothing declined
    CHECK(f1 > f0);                         // on: mirrors declined
    CHECK(on == off);
}

TEST_CASE("Sound fold F3: the shuffle is order-blind only over DISTINCT card numbers")
{
    // Library::ShuffleByKey orders cards by a key of (seed, ordinal, card NUMBER): two orders of one set of distinct
    // numbers shuffle to one order -- F3's premise.
    Library a, b;
    for (int n : { 7, 3, 11, 5, 2 })  { Card c; c.m_number = n; a.push_back(c); }
    for (int n : { 3, 7, 11, 5, 2 })  { Card c; c.m_number = n; b.push_back(c); }
    a.ShuffleByKey(99); b.ShuffleByKey(99);
    for (std::size_t i = 0; i < a.size(); ++i) { CHECK(a[i].m_number == b[i].m_number); }
    // Two cards sharing a number TIE, and the tie keeps their input order: the swap survives the shuffle. (The
    // rollout files a resolved spell as its number-0 template; an Eldrazi's graveyard shuffle then brings several
    // into the library -- why F3 requires distinct numbers, pbfan::Table::numbers_unique.)
    Library c, d;
    auto put = [](Library& l, int n, const char* nm) { Card x = DefSf(nm).card; x.m_number = n; l.push_back(x); };
    put(c, 0, "Ponder"); put(c, 0, "Brainstorm"); put(c, 5, "Island");
    put(d, 0, "Brainstorm"); put(d, 0, "Ponder"); put(d, 5, "Island");
    c.ShuffleByKey(99); d.ShuffleByKey(99);
    bool differ = false;
    for (std::size_t i = 0; i < c.size(); ++i) { if (c[i].m_name != d[i].m_name) { differ = true; } }
    CHECK(differ);
}

TEST_CASE("Sound fold F4 (default OFF): a rank that strands a later cast realises the smaller plan's same put-back")
{
    // Brainstorm then Goblin Guide (the only copy). A put-back of Goblin Guide strands its cast: the apply realises
    // "Brainstorm" with the same two cards on top.
    GameState s = BoardSf(1, true, { "Goblin Guide", "Lightning Bolt" }, { "Lightning Bolt", "Birds of Paradise", "Sol Ring" });
    PutSf(s, "Mountain", 60);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const std::map<std::string, int> b1 = FanOutcomesSf(s, plans, "land=;Brainstorm,Goblin Guide,");
    const std::map<std::string, int> b2 = FanOutcomesSf(s, plans, "land=;Brainstorm,");
    REQUIRE_FALSE(b1.empty());
    REQUIRE_FALSE(b2.empty());
    // Every B1 outcome with Goblin Guide still in the library (stranded) is a B2 outcome (taps are not in it).
    int stranded = 0;
    for (const auto& [o, k] : b1)
    {
        if (o.find("Goblin Guide") != std::string::npos) { continue; }   // cast: on the battlefield
        ++stranded;
        CHECK(b2.count(o) == 1);
    }
    CHECK(stranded > 0);
    // The lever is OFF by default; on, a lookahead solve declines the stranding ranks.
    const long long f0 = FoldCounterSf(2);
    { const TurnSolver::Plan p = TurnSolver::SolveWithLookahead(s, true, 2, 8, nullptr, false, false, nullptr); (void)p; }
    CHECK(FoldCounterSf(2) == f0);
    {
        ArmScopeSf _a(heurarm::PB_STRAND_FOLD, 1);
        const TurnSolver::Plan p = TurnSolver::SolveWithLookahead(s, true, 2, 8, nullptr, false, false, nullptr);
        (void)p;
    }
    CHECK(FoldCounterSf(2) > f0);
}

TEST_CASE("Sound fold F5: at a breakpoint-node host, a base whose prefix collapsed has its whole fan declined")
{
    // Brainstorm with two different tails (Goblin Guide / Lightning Bolt): the node host truncates both at the
    // Brainstorm, so their prefixes pend at one state and every rank of the second fan collapses onto the first's.
    GameState s = BoardSf(1, true, { "Goblin Guide", "Lightning Bolt" }, { "Lightning Bolt", "Birds of Paradise", "Sol Ring" });
    PutSf(s, "Mountain", 60);
    PutSf(s, "Mountain", 61);
    auto run = [&](int on) -> int
    {
        ArmScopeSf _a(heurarm::PB_XBASE_FOLD, on);
        SearchBudget u(0);   // unlimited
        const TurnSolver::SearchLine line = TurnSolver::FullSearchLine(s, 2, 8, false, nullptr, &u);
        return line.win_turn;
    };
    const long long f0 = FoldCounterSf(3);
    const int wt_on = run(1);
    const long long f1 = FoldCounterSf(3);
    const int wt_off = run(0);
    CHECK(FoldCounterSf(3) == f1);
    CHECK(f1 > f0);
    CHECK(wt_on == wt_off);
}
