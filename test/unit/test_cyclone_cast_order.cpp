// Unpredictable Cyclone, THE DRAFTED CAST ORDER and CYCLE ORDER (docs/design/cyclone-cast-order.md; USER review
// 2026-10-29: "The order looks reasonable, though we should also specify the cycle order").
//
//   * MTG_CYCLONE_CAST_ORDER (DEFAULT OFF): Long-Term Plans < Ponder < Brainstorm < Brainsurge < Remand < Seething Song <
//     Unpredictable Cyclone < Curator < Street Wraith < Ulamog CH < Ulamog IG < Emrakul, from params; Toxic Deluge never.
//     The draft's FUNDING ranges ride CastOrderFallbackRanks. Off -> the generic ranks.
//   * MTG_CYCLONE_ORDER_DRAW_TURNS (DEFAULT OFF): OrderOpaqueCastsByRank, only with the order on.
//   * MTG_CYCLONE_CYCLE_ORDER (DEFAULT 0): mode 1 = draft 2, mode 2 = cheapest first -- the SEARCHED cycle sources'
//     order (same names, re-sorted) and the PLAYOUT pick (SelectDigSource).
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
constexpr const char* kCycO    = "Unpredictable Cyclone";
constexpr const char* kWraO    = "Street Wraith";
constexpr const char* kCurO    = "Curator of Mysteries";
constexpr const char* kLoungeO = "Xander's Lounge";

void EnsureCardsO()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card CardO(const std::string& name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Card c     = d->card;
    c.m_number = number;
    return c;
}

const CardDefinition& DefO(const std::string& name)
{
    EnsureCardsO();
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

const CycloneProvider& ProvO() { static const CycloneProvider cp; return cp; }

struct ArmO
{
    heurarm::Arm saved;
    ArmO() : saved(heurarm::t_arm) {}
    ArmO& Set(heurarm::Slot slot, int v) { heurarm::t_arm[static_cast<int>(slot)] = static_cast<std::int8_t>(v); return *this; }
    ~ArmO() { heurarm::t_arm = saved; }
};

GameState StateO(const std::vector<std::string>& bf, const std::vector<std::string>& hand, int land_drops_used = 1)
{
    EnsureCardsO();
    GameState s;
    s.m_provider          = &ProvO();
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.on_the_play         = true;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = land_drops_used;
    s.deck_has_cycle_draw_replacement = true;
    int num = 100;
    for (const std::string& n : bf)
    {
        Permanent p;
        p.card             = CardO(n, num++);
        p.controller_index = 0;
        p.owner_index      = 0;
        s.battlefield.push_back(p);
    }
    for (const std::string& n : hand) { s.players[0].hand.push_back(CardO(n, num++)); }
    std::vector<Card> l;
    for (int k = 0; k < 12; ++k) { l.push_back(CardO("Island", 900 + k)); }
    s.players[0].library.assign(l.begin(), l.end());
    return s;
}

using NamesO = std::vector<std::string>;

std::string PlayoutPickO(const GameState& s)
{
    bool is_sac = false;
    return ProvO().SelectDigSource(s, AvailableManaPool(s), is_sac);
}
}   // namespace

TEST_CASE("Cyclone cast order: the drafted rank table, total and from params")
{
    const GameState s = StateO({}, {});
    const std::vector<std::string> order = {
        "Long-Term Plans", "Ponder", "Brainstorm", "Brainsurge", "Remand", "Seething Song", kCycO, kCurO, kWraO,
        "Ulamog, the Ceaseless Hunger", "Ulamog, the Infinite Gyre", "Emrakul, the Aeons Torn",
    };
    SUBCASE("lever ON: strictly increasing in the drafted order, every tier named, Toxic Deluge never")
    {
        ArmO arm;
        arm.Set(heurarm::CYCLONE_CAST_ORDER, 1);
        int prev = -1;
        for (const std::string& n : order)
        {
            const int r = ProvO().CastOrderRank(s, DefO(n));
            CHECK_MESSAGE(r > prev, n, " rank ", r, " must follow ", prev);
            CHECK_MESSAGE(ProvO().CastOrderTierName(r) != nullptr, n);
            prev = r;
        }
        CHECK(ProvO().CastOrderRank(s, DefO("Long-Term Plans")) == 10);
        CHECK(ProvO().CastOrderRank(s, DefO("Brainstorm")) == 31);
        CHECK(ProvO().CastOrderRank(s, DefO("Brainsurge")) == 33);
        CHECK(ProvO().CastOrderRank(s, DefO("Seething Song")) == 60);
        CHECK(ProvO().CastOrderRank(s, DefO(kCycO)) == 70);
        CHECK(ProvO().CastOrderRank(s, DefO("Emrakul, the Aeons Torn")) == 115);
        CHECK(ProvO().CastOrderRank(s, DefO("Toxic Deluge")) == 200);
        CHECK(ProvO().CastOrderTotalByName());
        // A card the order does not name lands after every named one.
        CHECK(ProvO().CastOrderRank(s, DefO("Lightning Bolt")) >= 300);
    }
    SUBCASE("lever OFF: the generic ranks (today: Curator 10 < Song 15 < Cyclone 20), not total")
    {
        ArmO arm;
        arm.Set(heurarm::CYCLONE_CAST_ORDER, 0);
        const GenericProvider gen;
        for (const std::string& n : order)
        { CHECK_MESSAGE(ProvO().CastOrderRank(s, DefO(n)) == gen.CastOrderRank(s, DefO(n)), n); }
        CHECK_FALSE(ProvO().CastOrderTotalByName());
        CHECK(ProvO().CastOrderFallbackRanks(s, DefO("Seething Song")).empty());
    }
}

TEST_CASE("Cyclone cast order: the FUNDING ranges ride the ladder; the draw-turn opt-in needs the order")
{
    const GameState s = StateO({}, {});
    ArmO arm;
    arm.Set(heurarm::CYCLONE_CAST_ORDER, 1);
    CHECK(ProvO().CastOrderFallbackRanks(s, DefO("Brainstorm")) == std::vector<int>{ 31, 71 });
    CHECK(ProvO().CastOrderFallbackRanks(s, DefO("Brainsurge")) == std::vector<int>{ 33, 65 });
    CHECK(ProvO().CastOrderFallbackRanks(s, DefO("Remand")) == std::vector<int>{ 50, 72 });
    CHECK(ProvO().CastOrderFallbackRanks(s, DefO("Seething Song")) == std::vector<int>{ 60, 32, 5 });
    // The information ranges (LTP / Ponder: the shuffle line) are not payability questions: no ladder.
    CHECK(ProvO().CastOrderFallbackRanks(s, DefO("Long-Term Plans")).empty());
    CHECK(ProvO().CastOrderFallbackRanks(s, DefO("Ponder")).empty());
    CHECK_FALSE(ProvO().OrderOpaqueCastsByRank());
    arm.Set(heurarm::CYCLONE_ORDER_DRAW_TURNS, 1);
    CHECK(ProvO().OrderOpaqueCastsByRank());
    arm.Set(heurarm::CYCLONE_CAST_ORDER, 0);
    CHECK_FALSE(ProvO().OrderOpaqueCastsByRank());   // inert without the order
}

TEST_CASE("Cyclone cycle order mode 1 (draft 2): Cyclone OUT")
{
    ArmO arm;
    arm.Set(heurarm::CYCLONE_CYCLE_ORDER, 1);
    const NamesO hand = { kWraO, kLoungeO, kCycO, kCurO };
    SUBCASE("a {U} spare: Curator, Wraith, Lounge, the surplus Cyclone -- searched order and the playout pick")
    {
        const GameState s = StateO({ kCycO, "Island", "Island" }, hand);
        CHECK(ProvO().SearchedCycleSources(s) == NamesO{ kCurO, kWraO, kLoungeO, kCycO });
        CHECK(PlayoutPickO(s) == kCurO);
    }
    SUBCASE("no {U} spare (a Brainstorm needs the only other land): Wraith first, Curator after it")
    {
        GameState s = StateO({ kCycO, "Island" }, hand);
        s.players[0].hand.push_back(CardO("Brainstorm", 300));
        CHECK(ProvO().SearchedCycleSources(s) == NamesO{ kWraO, kCurO, kLoungeO, kCycO });
        CHECK(PlayoutPickO(s) == kWraO);
    }
    SUBCASE("lever off: today's order (creatures, other nonlands, lands; cheaper first) and today's pick (Wraith)")
    {
        arm.Set(heurarm::CYCLONE_CYCLE_ORDER, 0);
        const GameState s = StateO({ kCycO, "Island", "Island" }, hand);
        CHECK(ProvO().SearchedCycleSources(s) == NamesO{ kWraO, kCurO, kCycO, kLoungeO });
        CHECK(PlayoutPickO(s) == kWraO);
    }
}

TEST_CASE("Cyclone cycle order mode 1 (draft 2): Cyclone NOT out")
{
    ArmO arm;
    arm.Set(heurarm::CYCLONE_CYCLE_ORDER, 1);
    SUBCASE("two Cyclones held: the surplus one first, then Lounge (not the drop), Curator, Wraith last")
    {
        const GameState s = StateO({ "Island", "Island", "Mountain" }, { kWraO, kCurO, kLoungeO, kCycO, kCycO, "Island" },
                                   /*land_drops_used=*/0);
        CHECK(ProvO().SearchedCycleSources(s) == NamesO{ kCycO, kLoungeO, kCurO, kWraO });
        CHECK(PlayoutPickO(s) == kCycO);
    }
    SUBCASE("the Lounge IS the land drop (drop open, no other land): after Wraith; the only Cyclone last")
    {
        const GameState s = StateO({ "Island", "Island", "Mountain" }, { kWraO, kCurO, kLoungeO, kCycO },
                                   /*land_drops_used=*/0);
        CHECK(ProvO().SearchedCycleSources(s) == NamesO{ kCurO, kWraO, kLoungeO, kCycO });
        CHECK(PlayoutPickO(s) == kCurO);
    }
    SUBCASE("drop used: the Lounge is a plain draw again, ahead of the creatures")
    {
        const GameState s = StateO({ "Island", "Island", "Mountain" }, { kWraO, kCurO, kLoungeO, kCycO });
        CHECK(ProvO().SearchedCycleSources(s) == NamesO{ kLoungeO, kCurO, kWraO, kCycO });
        CHECK(PlayoutPickO(s) == kLoungeO);
    }
    SUBCASE("the playout takes the first PAYABLE: one Island up -> Curator ({U}); nothing up -> Wraith")
    {
        GameState s = StateO({ "Island" }, { kWraO, kCurO, kLoungeO, kCycO, kCycO });
        CHECK(PlayoutPickO(s) == kCurO);   // Cyclone {2}, Lounge {3} unpayable
        s.battlefield[0].tapped = true;
        CHECK(PlayoutPickO(s) == kWraO);
    }
}

TEST_CASE("Cyclone cycle order mode 2: the USER's cheapest first")
{
    ArmO arm;
    arm.Set(heurarm::CYCLONE_CYCLE_CHEAP, 1);
    CHECK(CycloneCycleOrderMode() == 2);
    const GameState s = StateO({ kCycO, "Island", "Island", "Island" }, { kLoungeO, kCycO, kCurO, kWraO });
    CHECK(ProvO().SearchedCycleSources(s) == NamesO{ kWraO, kCurO, kCycO, kLoungeO });
    CHECK(PlayoutPickO(s) == kWraO);
    arm.Set(heurarm::CYCLONE_CYCLE_ORDER, 1);   // mode 1 wins when both are set
    CHECK(CycloneCycleOrderMode() == 1);
}

// ---- THE SEED PAYMENT SPLIT (MTG_CYCLE_SEED_PAY_SPLIT, ledger Q61) ------------------------------------------------
namespace
{
std::vector<int> SplitUnitsO(std::int32_t v)
{
    std::vector<int> u;
    for (int b = 0; b < 4; ++b) { const int x = ((v >> (8 * b)) & 0xFF) - 1; if (x < 0) { break; } u.push_back(x); }
    return u;
}
constexpr int kSigU = 1 << static_cast<int>(Color::Blue);
constexpr int kSigR = 1 << static_cast<int>(Color::Red);
int CountO(const std::vector<int>& u, int sig) { return static_cast<int>(std::count(u.begin(), u.end(), sig)); }
}   // namespace

TEST_CASE("Seed payment split: off by default; on, one split per colour multiset, the hand-demand one first")
{
    const NamesO lands = { "Mountain", "Mountain", "Mountain", "Island", "Island", "Island", "Island" };
    const GameState s = StateO(lands, { kCycO, kCycO });
    const ManaCost two = DefO(kCycO).params.cycling_cost.value();
    SUBCASE("lever off: no split")
    {
        ArmO arm;
        arm.Set(heurarm::CYCLE_SEED_PAY_SPLIT, 0);
        CHECK(SeedPaySplits(s, 0, two, DefO(kCycO)).empty());
    }
    SUBCASE("three Mountains + four Islands, the other Cyclone held: RR / RU / UU, and RR (it strands {R}{R}) LAST")
    {
        ArmO arm;
        arm.Set(heurarm::CYCLE_SEED_PAY_SPLIT, 1);
        const std::vector<std::int32_t> sp = SeedPaySplits(s, 0, two, DefO(kCycO));
        REQUIRE(sp.size() == 3);
        CHECK(CountO(SplitUnitsO(sp[0]), kSigR) <= 1);   // split 0 leaves the held Cyclone's {R}{R}
        CHECK(CountO(SplitUnitsO(sp[2]), kSigR) == 2);
        // Realised: every untapped source but the split's is HELD.
        const std::uint64_t hold = SeedPaySplitHoldMask(s, 0, sp[2]);
        CHECK(hold == ((1ull << 2) | (1ull << 3) | (1ull << 4) | (1ull << 5) | (1ull << 6)));   // Mountains 0, 1 pay
        // A split this board cannot realise -> 0 (the payer decides).
        GameState t = s;
        t.battlefield[0].tapped = true;
        t.battlefield[1].tapped = true;
        CHECK(SeedPaySplitHoldMask(t, 0, sp[2]) == 0);
    }
    SUBCASE("DOMINANCE: a dual is never tapped while a mono with a subset of its colours is left -> one split, no fan")
    {
        ArmO arm;
        arm.Set(heurarm::CYCLE_SEED_PAY_SPLIT, 1);
        const GameState v = StateO({ "Mountain", "Mountain", "Steam Vents" }, { kCycO, kCycO });
        CHECK(SeedPaySplits(v, 0, two, DefO(kCycO)).empty());
        const GameState w = StateO({ "Mountain", "Island", "Steam Vents" }, { kCycO, kCycO });
        CHECK(SeedPaySplits(w, 0, two, DefO(kCycO)).empty());   // only {Mountain, Island} is undominated
    }
    SUBCASE("a coloured cost (Curator's {U}) and a single source class never fan")
    {
        ArmO arm;
        arm.Set(heurarm::CYCLE_SEED_PAY_SPLIT, 1);
        CHECK(SeedPaySplits(s, 0, DefO(kCurO).params.cycling_cost.value(), DefO(kCurO)).empty());
        const GameState m = StateO({ "Mountain", "Mountain", "Mountain" }, { kCycO, kCycO });
        CHECK(SeedPaySplits(m, 0, two, DefO(kCycO)).empty());
    }
}
