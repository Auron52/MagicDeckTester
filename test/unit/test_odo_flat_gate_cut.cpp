// Unit cover for the Hydras runaway-enumeration fixes (2026-11-12; docs/design/uncharged-enumeration-backstop-hole.md):
//
//   * MTG_ODO_FLAT_GATE_CUT -- EnumeratePlanPositions' flat fallback skips a whole block of odometer positions
//     when a lower bound on (need - budget) over every completion of the block's high digits already exceeds
//     the pool. The cut is only legal if it is SOUND (every skipped position emits nothing) and ORDER-
//     PRESERVING (the emitted selection sequence is unchanged), so both are asserted here against the walk
//     run whole: on a Hydras-shaped board (the case that ran 4.5 h) and on a few thousand random gates that
//     exercise every term the bound prices (soft reducers, hard blocks, the graveyard triangle, the land-tap
//     x land-put cross term, independents).
//   * MTG_SEL_GATE_SELF_EXCL -- a same-turn reducer never discounts its own cast (Gargos pays {5}{G} in full).
//   * MTG_ENUM_WALK_POLL -- a game that turns VOID mid-enumeration stores nothing in any per-game plan cache,
//     and the next live lookup returns exactly the cold list (a truncated list is never served later).
#include <doctest/doctest.h>

#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <array>
#include <random>
#include <string>
#include <vector>

// TurnSolver.cpp test hooks.
extern std::vector<long long> OdoFlatCutAuditForTest(const std::vector<std::array<int, 9>>& terms,
                                                     const std::vector<std::vector<int>>& groups,
                                                     const std::vector<int>& independent,
                                                     int pool, int ltb_all, bool soft_exact);
extern std::vector<long long> VoidGameEnumStoreForTest(const GameState& state);

namespace
{
// {cost, gain, gy, block, soft, red, gen, picks, ltb}
using Term = std::array<int, 9>;
enum { kPositions, kSkipped, kEmitWhole, kEmitCut, kSameSeq, kCutOn };

struct SelfExclArm
{
    explicit SelfExclArm(bool on) { heurarm::t_arm[heurarm::SEL_GATE_SELF_EXCL] = on ? 1 : 0; }
    ~SelfExclArm() { heurarm::Clear(); }
};

// The Hydras board of gi 287 in miniature: a 3-mana pool, Gargos ({5}{G}, "Hydra spells cost {4} less"), two
// X Hydras (X 0..5, {X}{G}{G}), Kalonian Hydra, Vernal Bloom (a land-tap bonus source) and Nature's Lore (an
// untapped land-put whose cands-wide cross credit counts the Bloom).
struct HydraBoard
{
    std::vector<Term> terms;
    std::vector<std::vector<int>> groups;
    std::vector<int> independent;
    int pool = 3, ltb_all = 1;
    HydraBoard()
    {
        terms.push_back({ 6, 0, 0, 1, 1, 4, 5, 0, 0 });                         // 0 Gargos
        groups.push_back({ 0 });
        for (int h = 0; h < 2; ++h)                                           // X Hydras
        {
            std::vector<int> g;
            for (int x = 0; x <= 5; ++x) { g.push_back(static_cast<int>(terms.size())); terms.push_back({ x + 2, 0, 0, 0, 0, 0, x, 0, 0 }); }
            groups.push_back(g);
        }
        groups.push_back({ static_cast<int>(terms.size()) }); terms.push_back({ 5, 0, 0, 0, 0, 0, 3, 0, 0 });   // Kalonian
        groups.push_back({ static_cast<int>(terms.size()) }); terms.push_back({ 4, 0, 0, 0, 0, 0, 3, 0, 1 });   // Vernal Bloom
        independent.push_back(static_cast<int>(terms.size()));
        terms.push_back({ 2, 1 + 1 * ltb_all, 0, 0, 0, 0, 1, 1, 0 });                                          // Nature's Lore
    }
};
}  // namespace

TEST_CASE("odo flat gate cut: Hydras board -- skips blocks, emits exactly what the whole walk emits")
{
    const HydraBoard b;
    for (const bool self_excl : { false, true })
    {
        CAPTURE(self_excl);
        SelfExclArm arm(self_excl);
        const auto r = OdoFlatCutAuditForTest(b.terms, b.groups, b.independent, b.pool, b.ltb_all, /*soft_exact=*/true);
        REQUIRE(r.size() == 6);
        CHECK(r[kCutOn] == 1);
        CHECK(r[kSkipped] > 0);                  // the cut does fire on this shape
        CHECK(r[kEmitWhole] == r[kEmitCut]);
        CHECK(r[kSameSeq] == 1);                 // sound and order-preserving
    }
}

TEST_CASE("odo flat gate cut: without the emission test every soft reducer is a hard block, and it still holds")
{
    const HydraBoard b;
    const auto r = OdoFlatCutAuditForTest(b.terms, b.groups, b.independent, b.pool, b.ltb_all, /*soft_exact=*/false);
    REQUIRE(r.size() == 6);
    CHECK(r[kSameSeq] == 1);
    CHECK(r[kSkipped] > 0);   // blocks above the Gargos digit with no Gargos chosen are still cut
}

TEST_CASE("sel gate self-exclusion: Gargos alone on 3 mana is refused only with the reducer's own discount excluded")
{
    const std::vector<Term> terms = { { 6, 0, 0, 1, 1, 4, 5, 0, 0 } };
    const std::vector<std::vector<int>> groups = { { 0 } };
    long long emits_off = -1, emits_on = -1;
    {
        SelfExclArm arm(false);
        const auto r = OdoFlatCutAuditForTest(terms, groups, {}, 3, 0, true);
        emits_off = r[kEmitWhole];
        CHECK(r[kSameSeq] == 1);
    }
    {
        SelfExclArm arm(true);
        const auto r = OdoFlatCutAuditForTest(terms, groups, {}, 3, 0, true);
        emits_on = r[kEmitWhole];
        CHECK(r[kSameSeq] == 1);
    }
    CHECK(emits_off == 1);   // old bound: 6 - min(5, 4) = 2 <= 3, admitted
    CHECK(emits_on == 0);    // Gargos pays its own {5}{G}: 6 > 3

    // Two Gargos (a Hydra himself): the SECOND is discounted by the first, so on 8 mana the pair (6 + 2 = 8) is
    // payable and must still be admitted with the self-exclusion on.
    const std::vector<Term> two = { { 6, 0, 0, 1, 1, 4, 5, 0, 0 }, { 6, 0, 0, 1, 1, 4, 5, 0, 0 } };
    SelfExclArm arm(true);
    const auto r2 = OdoFlatCutAuditForTest(two, { { 0 }, { 1 } }, {}, 8, 0, true);
    CHECK(r2[kEmitWhole] == 3);   // each alone (6 <= 8) and the pair (6 + 2 = 8)
    CHECK(r2[kSameSeq] == 1);
}

TEST_CASE("odo flat gate cut: randomized gates -- never skips an emitting position, never reorders")
{
    std::mt19937 rng(20261112u);
    auto pick = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    long long skipped = 0, armed = 0, cases = 0;
    for (int it = 0; it < 3000; ++it)
    {
        const int ng = pick(1, 5), ni = pick(0, 2);
        std::vector<Term> terms;
        std::vector<std::vector<int>> groups(ng);
        std::vector<int> independent;
        auto make = [&]
        {
            Term t{};
            const int cost = pick(0, 7);
            t[0] = cost;
            t[6] = pick(0, cost);                                     // gen <= cost
            t[1] = pick(0, 6) == 0 ? pick(1, 3) : 0;                 // gain (the cross credit is added below)
            t[2] = pick(0, 9) == 0 ? 1 : 0;                          // Rite-of-Flame graveyard ritual
            const int kind = pick(0, 19);
            if (kind < 3) { t[3] = 1; t[4] = 1; t[5] = pick(1, 4); } // soft generic reducer
            else if (kind == 3) { t[3] = 1; }                         // hard block (affinity, Hollow One, ...)
            if (pick(0, 4) == 0) { t[7] = 1; }                        // an untapped land-put
            if (pick(0, 6) == 0) { t[8] = pick(1, 2); }               // a land-tap bonus source
            return t;
        };
        for (int g = 0; g < ng; ++g)
        {
            const int opts = pick(1, 4);
            for (int o = 0; o < opts; ++o) { groups[g].push_back(static_cast<int>(terms.size())); terms.push_back(make()); }
        }
        for (int b = 0; b < ni; ++b) { independent.push_back(static_cast<int>(terms.size())); terms.push_back(make()); }
        int ltb_all = 0;
        for (const Term& t : terms) { ltb_all += t[8]; }
        for (Term& t : terms) { t[1] += t[7] * ltb_all; }             // LandTapCrossBoundCredit, cands-wide
        const int pool = pick(0, 6);
        const bool soft_exact = pick(0, 1) == 1;
        SelfExclArm arm(pick(0, 1) == 1);
        const auto r = OdoFlatCutAuditForTest(terms, groups, independent, pool, ltb_all, soft_exact);
        REQUIRE(r.size() == 6);
        if (r[kSameSeq] != 1)
        {
            FAIL_CHECK("cut changed the emitted sequence on case " << it << " (pool " << pool
                       << ", soft_exact " << soft_exact << ")");
        }
        skipped += r[kSkipped];
        armed += r[kCutOn];
        ++cases;
    }
    CHECK(cases == 3000);
    CHECK(armed > 0);
    CHECK(skipped > 0);   // the property is not vacuous
}

TEST_CASE("enum walk poll: a game voided mid-enumeration stores nothing, and the next live lookup is the cold list")
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    GameState s;
    s.active_player_index = 0;
    s.turn_number = 5;
    s.players[0].lands_played_this_turn = 1;
    int num = 100;
    auto def = [](const std::string& n) -> const CardDefinition&
    {
        const CardDefinition* d = CardDatabase::Instance().Lookup(n);
        REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", n);
        return *d;
    };
    for (int k = 0; k < 12; ++k)
    {
        Permanent p;
        p.card = def(k < 6 ? "Forest" : "Plains").card;
        p.card.m_number = num++;
        p.controller_index = 0;
        p.owner_index = 0;
        s.battlefield.push_back(p);
    }
    // Eleven DISTINCT cheap creatures: 2^11 subsets, well past one poll stride of odometer positions.
    for (const char* n : { "Avacyn's Pilgrim", "Boreal Druid", "Elvish Mystic", "Fyndhorn Elves", "Llanowar Elves",
                           "Muscle Sliver", "Predatory Sliver", "Gladecover Scout", "Plated Sliver", "Sinew Sliver",
                           "Worthy Knight" })
    {
        Card c = def(n).card;
        c.m_number = num++;
        s.players[0].hand.push_back(c);
    }
    for (int k = 0; k < 20; ++k)
    {
        Card c = def("Forest").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    const auto r = VoidGameEnumStoreForTest(s);
    REQUIRE(r.size() == 8);
    CAPTURE(r[2]); CAPTURE(r[3]); CAPTURE(r[4]); CAPTURE(r[5]);
    CHECK(r[7] == 1);          // the game really was voided
    CHECK(r[1] == r[0]);       // no per-game cache grew while it was void
    CHECK(r[3] < r[5]);        // the void list WAS truncated (the walk's poll stopped it)
    CHECK(r[5] > 0);
    CHECK(r[4] == r[5]);       // the live lookup after it is the full list...
    CHECK(r[6] == 1);          // ...plan for plan
}
