// Unit tests for discard_dist, the shared distance-to-castable model of the cleanup-discard buckets
// (docs/design/discard-distance-model.md). Every expected value is derived by hand from the model's
// definition: hypergeometric land draws, kept hand lands played one per turn, a 0.75 discount per turn.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <array>
#include <string>
#include <vector>

namespace
{

using discard_dist::Census;
using discard_dist::LandDrop;

void EnsureCardsDist()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

constexpr double kG = discard_dist::kGamma;
constexpr int kW = 0, kU = 1, kG_ = 4;   // colour slots W U B R G

Census Lib(int board_mana, int lib_n, int lib_lands)
{
    Census c;
    c.board_mana = board_mana;
    c.lib_n      = lib_n;
    c.lib_lands  = lib_lands;
    return c;
}

double FactorOf(const std::array<double, 4>& p)
{
    // sum (P(t) - P(t-1)) g^(t-1) + (1 - P(4)) 0.02 g^4, written out independently of CastFactor.
    return p[0] + (p[1] - p[0]) * kG + (p[2] - p[1]) * kG * kG + (p[3] - p[2]) * kG * kG * kG
         + (1.0 - p[3]) * 0.02 * kG * kG * kG * kG;
}

}   // namespace

TEST_CASE("discard_dist: HyperAtLeast against hand-computed hypergeometric tails")
{
    using discard_dist::HyperAtLeast;
    CHECK(HyperAtLeast(10, 4, 1, 1) == doctest::Approx(0.4));
    CHECK(HyperAtLeast(10, 4, 2, 1) == doctest::Approx(1.0 - (6.0 / 10) * (5.0 / 9)));
    CHECK(HyperAtLeast(10, 4, 2, 2) == doctest::Approx((4.0 / 10) * (3.0 / 9)));
    CHECK(HyperAtLeast(10, 4, 3, 0) == 1.0);
    CHECK(HyperAtLeast(10, 0, 3, 1) == 0.0);    // no successes
    CHECK(HyperAtLeast(10, 4, 1, 2) == 0.0);    // k > n
    CHECK(HyperAtLeast(0, 0, 3, 1) == 0.0);     // empty library
    CHECK(HyperAtLeast(3, 3, 5, 3) == 1.0);     // draws capped at the library
}

TEST_CASE("discard_dist: castable now -> factor 1; a kept untapped land closes one short")
{
    const std::array<int, 5> none{};
    const auto now = discard_dist::CastProbByTurn(Lib(3, 30, 12), 3, none, {});
    CHECK(now == std::array<double, 4>{ 1.0, 1.0, 1.0, 1.0 });
    CHECK(discard_dist::CastFactor(now) == doctest::Approx(1.0));

    const auto one = discard_dist::CastProbByTurn(Lib(2, 30, 12), 3, none, { LandDrop{1, 0, false} });
    CHECK(one[0] == 1.0);
    CHECK(discard_dist::CastFactor(one) == doctest::Approx(1.0));
}

TEST_CASE("discard_dist: a kept TAPPED land arrives a turn late; three kept lands close three short in three turns")
{
    const std::array<int, 5> none{};
    const auto tapped = discard_dist::CastProbByTurn(Lib(2, 30, 0), 3, none, { LandDrop{1, 0, true} });
    CHECK(tapped == std::array<double, 4>{ 0.0, 1.0, 1.0, 1.0 });
    CHECK(discard_dist::CastFactor(tapped) == doctest::Approx(kG));

    const std::vector<LandDrop> three(3, LandDrop{1, 0, false});
    const auto t3 = discard_dist::CastProbByTurn(Lib(0, 30, 0), 3, none, three);
    CHECK(t3 == std::array<double, 4>{ 0.0, 0.0, 1.0, 1.0 });
    CHECK(discard_dist::CastFactor(t3) == doctest::Approx(kG * kG));   // the old table said 0.10
}

TEST_CASE("discard_dist: one short with no land kept draws at the library's land density")
{
    const std::array<int, 5> none{};
    const auto p = discard_dist::CastProbByTurn(Lib(2, 10, 4), 3, none, {});
    const std::array<double, 4> want = {
        0.4,
        1.0 - (6.0 / 10) * (5.0 / 9),
        1.0 - (6.0 / 10) * (5.0 / 9) * (4.0 / 8),
        1.0 - (6.0 / 10) * (5.0 / 9) * (4.0 / 8) * (3.0 / 7),
    };
    for (int t = 0; t < 4; ++t) { CHECK(p[static_cast<std::size_t>(t)] == doctest::Approx(want[static_cast<std::size_t>(t)])); }
    CHECK(discard_dist::CastFactor(p) == doctest::Approx(FactorOf(want)));
    // A land-light library is worse: 1 land in 10.
    const auto thin = discard_dist::CastProbByTurn(Lib(2, 10, 1), 3, none, {});
    CHECK(discard_dist::CastFactor(thin) < discard_dist::CastFactor(p));
}

TEST_CASE("discard_dist: colour -- enough mana but one white source short of WW")
{
    Census c = Lib(5, 10, 4);
    c.board_src[kW] = 1;
    c.lib_src[kW]   = 2;
    const std::array<int, 5> ww = { 2, 0, 0, 0, 0 };
    // Needs one more W source: a drawn Plains (2 of 10) on a free land drop.
    const auto p = discard_dist::CastProbByTurn(c, 2, ww, {});
    CHECK(p[0] == doctest::Approx(0.2));
    CHECK(p[1] == doctest::Approx(1.0 - (8.0 / 10) * (7.0 / 9)));
    // A kept Plains supplies it next turn.
    const auto kept = discard_dist::CastProbByTurn(c, 2, ww, { LandDrop{1, 1u << kW, false} });
    CHECK(kept[0] == 1.0);
    // No white left anywhere: stranded; the factor is only the floor.
    Census dry = c;
    dry.lib_src[kW] = 0;
    const auto none = discard_dist::CastProbByTurn(dry, 2, ww, {});
    CHECK(none == std::array<double, 4>{ 0.0, 0.0, 0.0, 0.0 });
    CHECK(discard_dist::CastFactor(none) == doctest::Approx(0.02 * kG * kG * kG * kG));
}

TEST_CASE("discard_dist: the kept land that supplies the missing colour is played first")
{
    Census c = Lib(1, 20, 0);
    const std::array<int, 5> u = { 0, 1, 0, 0, 0 };
    // Kept: a Forest and a Breeding Pool (G/U). The Pool goes first because the board has no blue.
    const std::vector<LandDrop> kept = { LandDrop{1, 1u << kG_, false}, LandDrop{1, (1u << kG_) | (1u << kU), false} };
    const auto p = discard_dist::CastProbByTurn(c, 2, u, kept);
    CHECK(p[0] == 1.0);
}

TEST_CASE("discard_dist: extra mana by turn (a dork the caller will cast) counts")
{
    const std::array<int, 5> none{};
    const std::array<int, 4> extra = { 0, 1, 1, 1 };
    const auto p = discard_dist::CastProbByTurn(Lib(3, 30, 0), 4, none, {}, &extra);
    CHECK(p == std::array<double, 4>{ 0.0, 1.0, 1.0, 1.0 });
}

TEST_CASE("discard_dist: BuildCensus reads our mana permanents and the library's land / colour counts")
{
    EnsureCardsDist();
    GameState s;
    s.active_player_index = 0;
    int num = 1;
    auto put = [&](const std::string& n, int ctl)
    {
        Permanent p;
        p.card             = CardDatabase::Instance().Lookup(n)->card;
        p.card.m_number    = num++;
        p.controller_index = ctl;
        p.owner_index      = ctl;
        s.battlefield.push_back(p);
    };
    put("Plains", 0);
    put("Breeding Pool", 0);
    put("Sol Ring", 0);          // mana rock, 2 colourless
    put("Llanowar Elves", 0);    // mana creature
    put("Forest", 1);            // the opponent's -- not ours
    for (const char* n : { "Island", "Island", "Forest", "Lightning Bolt", "Lightning Bolt" })
    {
        Card c;
        c.m_name = n;
        c.RehashName();
        c.m_number = num++;
        s.players[0].library.push_back(c);
    }
    const Census c = discard_dist::BuildCensus(s, 0);
    CHECK(c.board_mana == 1 + 1 + 2 + 1);
    CHECK(c.board_src[kW] == 1);
    CHECK(c.board_src[kU] == 1);
    CHECK(c.board_src[kG_] == 2);     // Breeding Pool + Llanowar Elves
    CHECK(c.lib_n == 5);
    CHECK(c.lib_lands == 3);
    CHECK(c.lib_src[kU] == 2);
    CHECK(c.lib_src[kG_] == 1);

    const LandDrop farm = discard_dist::LandDropOf(CardDatabase::Instance().Lookup("Remote Farm"));
    CHECK(farm.mana == 2);
    CHECK(farm.tapped);
    CHECK(farm.colours == (1u << kW));
}
