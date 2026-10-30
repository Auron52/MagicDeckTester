// Unpredictable Cyclone, CANTRIP ITEM 1 (2026-10-28): the go-off estimate {k, c} and the SCORED put-back / Ponder
// order (docs/design/cantrip-search-optimization.md §3.2-3.3; ledger "Cantrip item 1").
//
//   * THE HOOK (DecisionProvider::CantripReadinessOf / ScoreTopDisposition): the engine stable-sorts a put-back
//     or Ponder fan by the provider's score inside the shared PutBackPickAt / PutBackCandidates /
//     TopDispositionCandidates. AN ORDER ONLY: the candidate SET is unchanged (pinned below), so the lever
//     (MTG_CYCLONE_CANTRIP_ORDER, default ON) is inert at an unlimited budget by construction.
//   * THE USER's rules: Emrakul at the walk position, a dead noncreature ABOVE it, two Eldrazi back (Emrakul first)
//     with two cycles, [next-turn card, Eldrazi] at k = 1, the least needed back at k >= 2, the shuffle line's
//     dead-card put-backs early only when admitted; Ponder by the SAME scorer, its shuffle always at index 0 or 1.
//   * KNOWLEDGE: the score reads no library card deeper than the ones the resolution places or reveals.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{

constexpr const char* kCyc     = "Unpredictable Cyclone";
constexpr const char* kCur     = "Curator of Mysteries";
constexpr const char* kWra     = "Street Wraith";
constexpr const char* kEmr     = "Emrakul, the Aeons Torn";
constexpr const char* kHun     = "Ulamog, the Ceaseless Hunger";
constexpr const char* kDel     = "Toxic Deluge";
constexpr const char* kTarnC   = "Scalding Tarn";
constexpr const char* kVentsC  = "Steam Vents";
constexpr const char* kPonderC = "Ponder";

void EnsureCardsCo()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card CardCo(const std::string& name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Card c     = d->card;
    c.m_number = number;
    return c;
}

const CycloneProvider& ProvCo() { static const CycloneProvider cp; return cp; }

struct ArmCo
{
    heurarm::Arm saved;
    // These tests read the FULL scored fan: MTG_CYCLONE_PB_PLANS is pinned off in every scope, so they hold whatever its
    // default (test_cyclone_cantrip_plans.cpp covers the plan-level prune).
    ArmCo() : saved(heurarm::t_arm) { heurarm::t_arm[static_cast<int>(heurarm::CYCLONE_PB_PLANS)] = 0; }
    ArmCo& Set(heurarm::Slot slot, int v) { heurarm::t_arm[static_cast<int>(slot)] = static_cast<std::int8_t>(v); return *this; }
    ~ArmCo() { heurarm::t_arm = saved; }
};

// Round 3, main 1. `bf` on our battlefield (untapped), `hand` the post-draw hand (the cast cantrip is on the stack,
// not in hand), `lib` on top of the library over filler Islands.
GameState StateCo(const std::vector<std::string>& bf, const std::vector<std::string>& hand,
                  const std::vector<std::string>& lib = {}, int land_drops_used = 1, int filler = 12,
                  const char* filler_name = "Island")
{
    EnsureCardsCo();
    GameState s;
    s.m_provider          = &ProvCo();
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.on_the_play         = true;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = land_drops_used;
    s.deck_has_extra_turn = true;
    s.deck_has_cycle_draw_replacement = true;
    int num = 100;
    for (const std::string& n : bf)
    {
        Permanent p;
        p.card             = CardCo(n, num++);
        p.controller_index = 0;
        p.owner_index      = 0;
        s.battlefield.push_back(p);
    }
    for (const std::string& n : hand) { s.players[0].hand.push_back(CardCo(n, num++)); }
    for (const std::string& n : lib)  { s.players[0].library.push_back(CardCo(n, num++)); }
    for (int k = 0; k < filler; ++k)  { s.players[0].library.push_back(CardCo(filler_name, 900 + k)); }
    return s;
}

using Names = std::vector<std::string>;

// Candidate r of the put-back fan as card NAMES, top first.
Names PickNames(const GameState& s, int r)
{
    const PutBackPick pk = PutBackPickAt(s, 0, 2, r);
    Names out;
    for (int j = 0; j < pk.take; ++j) { out.push_back(s.players[0].hand[static_cast<std::size_t>(pk.idx[j])].m_name.str()); }
    return out;
}

std::multiset<Names> AllPicks(const GameState& s)
{
    std::multiset<Names> out;
    const int n = PutBackPickAt(s, 0, 2, 0).count;
    for (int r = 0; r < n; ++r) { out.insert(PickNames(s, r)); }
    return out;
}

std::vector<Names> OrderedPicks(const GameState& s)
{
    std::vector<Names> out;
    const int n = PutBackPickAt(s, 0, 2, 0).count;
    for (int r = 0; r < n; ++r) { out.push_back(PickNames(s, r)); }
    return out;
}

CantripReadiness ReadyCo(const GameState& s)
{
    CantripReadiness r;
    REQUIRE(ProvCo().CantripReadinessOf(s, 0, r));
    return r;
}

std::vector<Card> LookedCo(const GameState& s, int n)
{ return std::vector<Card>(s.players[0].library.begin(), s.players[0].library.begin() + n); }

Names DispNames(const TopDisposition& d, const std::vector<Card>& looked)
{
    if (d.shuffle) { return { "SHUFFLE" }; }
    const TopDisposition c = CanonicalReorder(d, static_cast<int>(looked.size()));
    Names out;
    for (int i : c.top_order) { out.push_back(looked[static_cast<std::size_t>(i)].m_name.str()); }
    return out;
}

struct DrawAfterCo
{
    int saved;
    explicit DrawAfterCo(int d) : saved(g_reorder_draw_after) { g_reorder_draw_after = d; }
    ~DrawAfterCo() { g_reorder_draw_after = saved; }
};

}   // namespace

TEST_CASE("Cyclone cantrip order: the go-off estimate {k, c} on hand shapes")
{
    ArmCo arm;
    arm.Set(heurarm::CYCLONE_CANTRIP_ORDER, 1);
    SUBCASE("a Cyclone out and a Street Wraith in hand: go off THIS turn, one cycle per Wraith")
    {
        const GameState s = StateCo({ kCyc, "Island", "Island", "Island" }, { kWra, kEmr, "Island" });
        const CantripReadiness r = ReadyCo(s);
        CHECK(r.k == 0);
        CHECK(r.c == 1);
        CHECK_FALSE(r.missing_piece);
    }
    SUBCASE("two Wraiths and a Curator with {U} open: c = 3")
    {
        const GameState s = StateCo({ kCyc, "Island", "Island" }, { kWra, kWra, kCur });
        const CantripReadiness r = ReadyCo(s);
        CHECK(r.k == 0);
        CHECK(r.c == 3);
    }
    SUBCASE("Cyclone in hand, five mana two turns out (k = 2), or next turn with the drop still open (k = 1)")
    {
        // 3 lands out (Steam Vents is the red pair with the Mountain in hand), drop already used this turn.
        const GameState s = StateCo({ kVentsC, "Island", "Island" }, { kCyc, kWra, "Mountain", "Island" });
        const CantripReadiness r = ReadyCo(s);
        CHECK(r.k == 2);   // turn 1: 3 + 1 = 4 mana; turn 2: 3 + 2 = 5 -> {3}{R}{R}
        CHECK(r.c >= 1);
        const GameState s1 = StateCo({ kVentsC, "Island", "Island" }, { kCyc, kWra, "Mountain", "Island" }, {}, 0);
        CHECK(ReadyCo(s1).k == 1);   // the drop still open this turn: 4 now, 3 + 2 = 5 next turn
    }
    SUBCASE("a missing piece: no Cyclone anywhere -> k = 3 (USER default: a missing piece means k >= 2)")
    {
        const GameState s = StateCo({ "Island", "Island" }, { kWra, kEmr, "Island" });
        const CantripReadiness r = ReadyCo(s);
        CHECK(r.missing_piece);
        CHECK(r.k == 3);
        const GameState s2 = StateCo({ kCyc, "Island" }, { kEmr, "Island", kDel });   // no creature cycler
        CHECK(ReadyCo(s2).missing_piece);
        CHECK(ReadyCo(s2).k == 3);
    }
    SUBCASE("keep needs: the sole Cyclone very high, a second copy surplus; Toxic Deluge dead")
    {
        const GameState s = StateCo({ "Island" }, { kCyc, kCyc, kDel, kWra });
        const CantripReadiness r = ReadyCo(s);
        REQUIRE(r.keep_need.size() == 4);
        CHECK(r.keep_need[0] == 1000);
        CHECK(r.keep_need[1] == 0);
        CHECK(r.keep_need[2] == 0);
        CHECK(r.keep_need[3] == 250);
        CHECK(r.dead_in_hand == 2);
    }
    SUBCASE("the lever off: no readiness (the existing order stands)")
    {
        ArmCo off;
        off.Set(heurarm::CYCLONE_CANTRIP_ORDER, 0).Set(heurarm::CYCLONE_PUTBACK_RULE, 0);
        const GameState s = StateCo({ kCyc }, { kWra, kEmr });
        CantripReadiness r;
        CHECK_FALSE(ProvCo().CantripReadinessOf(s, 0, r));
    }
}

TEST_CASE("Cyclone cantrip order: the put-back rank 0 follows the USER's rules")
{
    ArmCo arm;
    arm.Set(heurarm::CYCLONE_CANTRIP_ORDER, 1);
    SUBCASE("k = 0, one cycle: a dead noncreature ABOVE Emrakul (the walk bottoms it); Emrakul over Ulamog")
    {
        const GameState s = StateCo({ kCyc, "Island", "Island", "Island" }, { kWra, kEmr, kHun, kDel, "Island" });
        CHECK(PickNames(s, 0) == Names{ kDel, kEmr });
    }
    SUBCASE("k = 0, two cycles, two Eldrazi: both back, Emrakul first (on top)")
    {
        const GameState s = StateCo({ kCyc, "Island", "Island", "Island" }, { kWra, kWra, kEmr, kHun, kDel });
        CHECK(PickNames(s, 0) == Names{ kEmr, kHun });
    }
    SUBCASE("k = 0: never a non-Eldrazi creature above the Eldrazi")
    {
        const GameState s = StateCo({ kCyc, "Island", "Island", "Island" }, { kWra, kWra, kWra, kWra, kEmr });
        // Four Wraiths: one is surplus (beyond three); still the Emrakul goes on TOP of it, never under it.
        const Names p0 = PickNames(s, 0);
        REQUIRE(p0.size() == 2);
        CHECK(p0[0] == kEmr);
        const int n = PutBackPickAt(s, 0, 2, 0).count;
        int r_bad = -1, r_good = -1;
        for (int r = 0; r < n; ++r)
        {
            const Names p = PickNames(s, r);
            if (p == Names{ kWra, kEmr }) { r_bad = r; }
            if (p == Names{ kEmr, kWra }) { r_good = r; }
        }
        CHECK(r_good >= 0);
        CHECK(r_bad > r_good);
    }
    SUBCASE("k = 1: [next-turn card, Eldrazi]; with two Eldrazi the second stays in hand (USER default)")
    {
        // Cyclone in hand, 4 lands out, the 5th land to play next turn; two Wraiths.
        const GameState s = StateCo({ kVentsC, "Island", "Island", "Mountain" }, { kCyc, kWra, kWra, kEmr, kHun, "Island" });
        REQUIRE(ReadyCo(s).k == 1);
        const Names p0 = PickNames(s, 0);
        REQUIRE(p0.size() == 2);
        CHECK(p0[1] == kEmr);   // the walk's first creature after next turn's draw
        CHECK(p0[0] != kEmr);
        // Only ONE Eldrazi is placed for the walk: the top card is drawn in the draw step, so the Ulamog is either
        // kept in hand or rides position 1 (drawn back before go-off) -- never at a walk position.
    }
    SUBCASE("k >= 2 (a missing piece): the least needed back, the sooner-wanted one on top")
    {
        const GameState s = StateCo({ "Island" }, { kWra, kPonderC, kDel, "Island", "Mountain" });
        REQUIRE(ReadyCo(s).k >= 2);
        const Names p0 = PickNames(s, 0);
        REQUIRE(p0.size() == 2);
        CHECK(p0[1] == kDel);   // the dead card deepest
        CHECK(p0[0] != kWra);
    }
}

TEST_CASE("Cyclone cantrip order: the SHUFFLE LINE's put-backs rank early only when admitted")
{
    ArmCo arm;
    arm.Set(heurarm::CYCLONE_CANTRIP_ORDER, 1);
    // No Cyclone (a missing piece -> k = 3), two dead Deluges, an Emrakul in hand.
    const Names hand{ kDel, kDel, kEmr, kWra, "Island", kTarnC };
    SUBCASE("admitted: the land drop open and a Scalding Tarn in hand -> the two Deluges back, Emrakul kept")
    {
        const GameState s = StateCo({ "Island", "Island" }, hand, {}, /*land_drops_used=*/0);
        const CantripReadiness r = ReadyCo(s);
        CHECK(r.shuffle_after);
        CHECK(r.dead_in_hand == 2);
        const Names p0 = PickNames(s, 0);
        CHECK(p0 == Names{ kDel, kDel });
    }
    SUBCASE("not admitted (the drop is used: no shuffle after the cantrip) -> an Eldrazi goes back first")
    {
        const GameState s = StateCo({ "Island", "Island", "Island" }, hand, {}, /*land_drops_used=*/1);
        CHECK_FALSE(ReadyCo(s).shuffle_after);
        const Names p0 = PickNames(s, 0);
        CHECK(std::find(p0.begin(), p0.end(), std::string(kEmr)) != p0.end());
        // ...and the dead-only put-back is still in the fan (ORDER only).
        bool dump = false;
        for (const Names& p : OrderedPicks(s)) { if (p == Names{ kDel, kDel }) { dump = true; } }
        CHECK(dump);
    }
}

TEST_CASE("Cyclone cantrip order: ORDER ONLY -- the candidate SET is unchanged, the reference list agrees")
{
    const GameState s = StateCo({ kCyc, "Island", "Island", "Island" }, { kWra, kEmr, kHun, kDel, "Island", "Island", kCur });
    std::multiset<Names> on, off;
    std::vector<Names> on_order, off_order;
    {
        ArmCo a; a.Set(heurarm::CYCLONE_CANTRIP_ORDER, 1).Set(heurarm::CYCLONE_PUTBACK_RULE, 0);
        on = AllPicks(s);
        on_order = OrderedPicks(s);
        // The fast path and the reference enumeration name the same candidate at every rank.
        const std::vector<std::vector<int>> ref = PutBackCandidates(s, 0, 2);
        REQUIRE(static_cast<int>(ref.size()) == PutBackPickAt(s, 0, 2, 0).count);
        for (std::size_t r = 0; r < ref.size(); ++r)
        {
            const Names fast = PickNames(s, static_cast<int>(r));
            Names rn;
            for (int hi : ref[r]) { rn.push_back(s.players[0].hand[static_cast<std::size_t>(hi)].m_name.str()); }
            CHECK(fast == rn);
        }
    }
    {
        ArmCo a; a.Set(heurarm::CYCLONE_CANTRIP_ORDER, 0).Set(heurarm::CYCLONE_PUTBACK_RULE, 0);
        off = AllPicks(s);
        off_order = OrderedPicks(s);
    }
    CHECK(on == off);                 // the same candidates...
    CHECK(on.size() == off.size());   // ...the same count
    CHECK(on_order != off_order);     // ...in a different order (the lever does something here)

    SUBCASE("Ponder: the same 7 dispositions either way")
    {
        GameState p = StateCo({ kCyc, "Island", "Island" }, { kWra }, { kEmr, "Island", kCur });
        const std::vector<Card> looked = LookedCo(p, 3);
        DrawAfterCo da(1);
        std::multiset<Names> pon, poff;
        {
            ArmCo a; a.Set(heurarm::CYCLONE_CANTRIP_ORDER, 1);
            for (const TopDisposition& d : TopDispositionCandidates(p, looked, LookKind::Reorder, -1)) { pon.insert(DispNames(d, looked)); }
        }
        {
            ArmCo a; a.Set(heurarm::CYCLONE_CANTRIP_ORDER, 0);
            for (const TopDisposition& d : TopDispositionCandidates(p, looked, LookKind::Reorder, -1)) { poff.insert(DispNames(d, looked)); }
        }
        CHECK(pon.size() == 7);
        CHECK(pon == poff);
    }
}

TEST_CASE("Cyclone cantrip order: KNOWLEDGE -- deeper library cards never move the order")
{
    ArmCo arm;
    arm.Set(heurarm::CYCLONE_CANTRIP_ORDER, 1);
    const Names bf{ kCyc, "Island", "Island", "Island" };
    const Names hand{ kWra, kEmr, kHun, kDel, "Island" };
    const GameState a = StateCo(bf, hand, {}, 1, 12, "Island");
    const GameState b = StateCo(bf, hand, { kEmr, kEmr, kWra }, 1, 12, "Mountain");
    CHECK(OrderedPicks(a) == OrderedPicks(b));
    // Ponder: the same three looked at, different cards below them.
    DrawAfterCo da(1);
    GameState pa = StateCo(bf, { kWra }, { kEmr, "Island", kCur }, 1, 12, "Island");
    GameState pb = StateCo(bf, { kWra }, { kEmr, "Island", kCur, kEmr, kHun }, 1, 12, "Mountain");
    const std::vector<Card> la = LookedCo(pa, 3), lb = LookedCo(pb, 3);
    std::vector<Names> oa, ob;
    for (const TopDisposition& d : TopDispositionCandidates(pa, la, LookKind::Reorder, -1)) { oa.push_back(DispNames(d, la)); }
    for (const TopDisposition& d : TopDispositionCandidates(pb, lb, LookKind::Reorder, -1)) { ob.push_back(DispNames(d, lb)); }
    CHECK(oa == ob);
}

TEST_CASE("Cyclone cantrip order: Ponder uses the Brainstorm scorer; its shuffle is always at index 0 or 1")
{
    ArmCo arm;
    arm.Set(heurarm::CYCLONE_CANTRIP_ORDER, 1);
    DrawAfterCo da(1);
    SUBCASE("k = 0: draw the needed cycler now, Emrakul the next creature; the shuffle second")
    {
        GameState s = StateCo({ kCyc, "Island", "Island" }, { kPonderC }, { kEmr, "Island", kWra });
        const std::vector<Card> looked = LookedCo(s, 3);
        // No cycler in hand -> missing piece at the readiness (k = 3): the drawn-now need decides.
        const std::vector<TopDisposition> c = TopDispositionCandidates(s, looked, LookKind::Reorder, -1);
        REQUIRE(c.size() == 7);
        CHECK(DispNames(c[0], looked)[0] == kWra);
        CHECK((c[0].shuffle || c[1].shuffle));
        // The unpinned default IS rank 0 (the base plan resolves at index 0).
        CHECK(DispNames(HeuristicTopDisposition(s, looked, LookKind::Reorder, -1), looked) == DispNames(c[0], looked));
    }
    SUBCASE("k = 0 with a Wraith in hand: the drawn card first, then Emrakul the first creature")
    {
        GameState s = StateCo({ kCyc, "Island", "Island" }, { kWra }, { "Island", kEmr, kCur });
        const std::vector<Card> looked = LookedCo(s, 3);
        REQUIRE(ReadyCo(s).k == 0);
        const std::vector<TopDisposition> c = TopDispositionCandidates(s, looked, LookKind::Reorder, -1);
        const Names n0 = DispNames(c[0], looked);
        REQUIRE(n0.size() == 3);
        CHECK(n0[0] == kCur);   // the needed cycler drawn now
        // After the draw, the walk's FIRST CREATURE is Emrakul (a land above it is bottomed for free).
        CHECK(std::find(n0.begin() + 1, n0.end(), std::string(kEmr)) != n0.end());
        CHECK(n0[2] == kEmr);
        CHECK(c[1].shuffle);
    }
    SUBCASE("k >= 2, the look holds no Eldrazi and nothing needed: the shuffle FIRST")
    {
        GameState s = StateCo({ "Island" }, { kWra, kCyc }, { kDel, kDel, kPonderC });
        REQUIRE(ReadyCo(s).k >= 2);
        const std::vector<Card> looked = LookedCo(s, 3);
        const std::vector<TopDisposition> c = TopDispositionCandidates(s, looked, LookKind::Reorder, -1);
        CHECK(c[0].shuffle);
        CHECK_FALSE(c[1].shuffle);
    }
}

TEST_CASE("Cyclone cantrip order: MTG_CYCLONE_PUTBACK_RULE is rank 0 of the new score; SINGLE needs it")
{
    const GameState s = StateCo({ kCyc, "Island", "Island", "Island" }, { kWra, kEmr, kHun, kDel, "Island" });
    Names scored;
    {
        ArmCo a; a.Set(heurarm::CYCLONE_CANTRIP_ORDER, 1).Set(heurarm::CYCLONE_PUTBACK_RULE, 0);
        scored = PickNames(s, 0);
    }
    {
        ArmCo a; a.Set(heurarm::CYCLONE_CANTRIP_ORDER, 0).Set(heurarm::CYCLONE_PUTBACK_RULE, 1);
        CHECK(PickNames(s, 0) == scored);
        CHECK(scored == Names{ kDel, kEmr });   // NOT the old rule's Emrakul on top of the Deluge
        a.Set(heurarm::CYCLONE_PUTBACK_SINGLE, 1);
        CHECK(ProvCo().PutBackSingleCandidate(s));
    }
    {
        ArmCo a; a.Set(heurarm::CYCLONE_PUTBACK_RULE, 0).Set(heurarm::CYCLONE_PUTBACK_SINGLE, 1);
        CHECK_FALSE(ProvCo().PutBackSingleCandidate(s));
    }
}

TEST_CASE("Cyclone cantrip order: the generic provider never scores (every other deck byte-identical)")
{
    EnsureCardsCo();
    GameState s = StateCo({ "Island" }, { kWra, kEmr, kDel });
    s.m_provider = nullptr;
    CantripReadiness r;
    CHECK_FALSE(ResolveProvider(s).CantripReadinessOf(s, 0, r));
}

// THE PER-FAN RANKING CACHE (design item 8; a pure speedup): PutBackPickAt reads the scored order back from a
// per-thread cache keyed on CycloneProvider::CantripOrderKey + the candidate list. A hit must equal a fresh
// derivation, and every input the scorer reads must be in the key: after warming the cache on one state, each
// one-input variation below is checked rank-for-rank against the reference enumeration (PutBackCandidates,
// never cached).
TEST_CASE("Cyclone cantrip order: the ranking cache -- a hit equals the reference, every scorer input is keyed")
{
    ArmCo arm;
    arm.Set(heurarm::CYCLONE_CANTRIP_ORDER, 1).Set(heurarm::CYCLONE_PUTBACK_RULE, 0);
    auto check_ref = [](const GameState& s)
    {
        const std::vector<std::vector<int>> ref = PutBackCandidates(s, 0, 2);
        REQUIRE(static_cast<int>(ref.size()) == PutBackPickAt(s, 0, 2, 0).count);
        for (std::size_t r = 0; r < ref.size(); ++r)
        {
            const PutBackPick pk = PutBackPickAt(s, 0, 2, static_cast<int>(r));
            CHECK(pk.idx[0] == ref[r][0]);
            CHECK(pk.idx[1] == ref[r][1]);
        }
    };
    const GameState base = StateCo({ kVentsC, "Island", "Island", "Mountain" }, { kCyc, kWra, kWra, kEmr, kHun, "Island", kDel });
    check_ref(base);
    check_ref(base);   // every rank now a hit
    GameState tapped = base;      tapped.battlefield[1].tapped = true;            check_ref(tapped);
    GameState drop = base;        drop.players[0].lands_played_this_turn = 0;      check_ref(drop);
    GameState life = base;        life.players[0].life = 3;                        check_ref(life);
    GameState bonus = base;       bonus.players[0].bonus_land_drops_this_turn = 1; check_ref(bonus);
    GameState swapped = base;     std::swap(swapped.players[0].hand[0], swapped.players[0].hand[3]); check_ref(swapped);
    GameState gone = base;        gone.battlefield.pop_back();                     check_ref(gone);
    GameState more = base;        more.battlefield.push_back(more.battlefield[0]); check_ref(more);
    GameState cyc_out = base;
    {
        Permanent p; p.card = CardCo(kCyc, 777); p.controller_index = 0; p.owner_index = 0;
        cyc_out.battlefield.push_back(p);
    }
    check_ref(cyc_out);
    check_ref(base);   // back to the warmed state
    // The per-job flag is keyed: the rule alone scores the same fan, the order off does not score at all.
    { ArmCo a; a.Set(heurarm::CYCLONE_CANTRIP_ORDER, 0); check_ref(base); }
    { ArmCo a; a.Set(heurarm::CYCLONE_CANTRIP_ORDER, 0).Set(heurarm::CYCLONE_PUTBACK_RULE, 1); check_ref(base); }
}

// AMENDMENT (1) (ledger Q44; MTG_CYCLONE_PUTBACK_USABLE, from d5 gi13): a put-back card drawn back before go-off that
// cannot be USED before it returns costs nothing to put back (the Niv rule: "followed by the ones you can't use yet so
// you can draw them later"). gi13 (seed 102013) T1: Steam Vents tapped for the Brainstorm, the land drop used, no
// Cyclone (k = 3); the hand after the draws holds two Brainsurges ({2}{U}), two Remands ({1}{U}), two Ponders, the
// Brainstorm it drew, Xander's Lounge and a Scalding Tarn. Next turn projects 2 mana: a Remand at position 2 could be
// cast before it returns, a Brainsurge could not. The old score put back [Remand, Remand] (the lowest keep needs).
TEST_CASE("Cyclone cantrip order: amendment (1) -- a card that cannot be used before its redraw goes back first")
{
    const char* kBsurge = "Brainsurge";
    const char* kRemand = "Remand";
    GameState s = StateCo({ kVentsC }, { kBsurge, kBsurge, kPonderC, "Xander's Lounge", kPonderC, kRemand, "Brainstorm",
                                         kTarnC, kRemand });
    s.battlefield[0].tapped = true;
    s.turn_number = 1;
    {
        ArmCo a; a.Set(heurarm::CYCLONE_CANTRIP_ORDER, 1).Set(heurarm::CYCLONE_PUTBACK_USABLE, 1);
        const CantripReadiness r = ReadyCo(s);
        REQUIRE(r.k == 3);
        REQUIRE_FALSE(r.shuffle_after);
        const Names p0 = PickNames(s, 0);
        REQUIRE(p0.size() == 2);
        CHECK(p0[1] == kBsurge);   // position 2: the card that cannot be cast before it is drawn again
        CHECK(p0[0] != kRemand);
        CHECK(p0[1] != kRemand);
    }
    {
        ArmCo a; a.Set(heurarm::CYCLONE_CANTRIP_ORDER, 1).Set(heurarm::CYCLONE_PUTBACK_USABLE, 0);
        CHECK(PickNames(s, 0) == Names{ kRemand, kRemand });   // the pre-amendment pick (gi13's A arm)
    }
    SUBCASE("ORDER ONLY: the same candidate set either way")
    {
        std::multiset<Names> on, off;
        { ArmCo a; a.Set(heurarm::CYCLONE_CANTRIP_ORDER, 1).Set(heurarm::CYCLONE_PUTBACK_USABLE, 1); on = AllPicks(s); }
        { ArmCo a; a.Set(heurarm::CYCLONE_CANTRIP_ORDER, 1).Set(heurarm::CYCLONE_PUTBACK_USABLE, 0); off = AllPicks(s); }
        CHECK(on == off);
    }
    SUBCASE("a shuffle after the cantrip (the land drop open, a fetchland in hand) leaves the old score in force")
    {
        GameState t = s;
        t.players[0].lands_played_this_turn = 0;
        std::vector<Names> on, off;
        { ArmCo a; a.Set(heurarm::CYCLONE_CANTRIP_ORDER, 1).Set(heurarm::CYCLONE_PUTBACK_USABLE, 1);
          REQUIRE(ReadyCo(t).shuffle_after); on = OrderedPicks(t); }
        { ArmCo a; a.Set(heurarm::CYCLONE_CANTRIP_ORDER, 1).Set(heurarm::CYCLONE_PUTBACK_USABLE, 0); off = OrderedPicks(t); }
        CHECK(on == off);
    }
    SUBCASE("a card castable before its redraw keeps its charge: with three untapped lands the Remand is not free")
    {
        // The land drop open (no fetchland, so no shuffle): the Lounge could be played before its redraw too.
        GameState t = StateCo({ kVentsC, "Island", "Island" }, { kBsurge, kRemand, kPonderC, "Xander's Lounge", kRemand }, {}, 0);
        ArmCo a; a.Set(heurarm::CYCLONE_CANTRIP_ORDER, 1).Set(heurarm::CYCLONE_PUTBACK_USABLE, 1);
        REQUIRE(ReadyCo(t).k >= 2);
        // Everything here is castable now (3 mana): the amendment changes nothing, the old pick stands.
        std::vector<Names> on = OrderedPicks(t), off;
        { ArmCo b; b.Set(heurarm::CYCLONE_PUTBACK_USABLE, 0); off = OrderedPicks(t); }
        CHECK(on == off);
    }
}
