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
    SUBCASE("both cycle policies off: generic order and cheapest playout pick")
    {
        arm.Set(heurarm::CYCLONE_CYCLE_ORDER, 0).Set(heurarm::CYCLONE_CYCLER_RULE, 0);
        const GameState s = StateO({ kCycO, "Island", "Island" }, hand);
        CHECK(ProvO().SearchedCycleSources(s) == NamesO{ kWraO, kCurO, kCycO, kLoungeO });
        CHECK(PlayoutPickO(s) == kWraO);
    }
    SUBCASE("cycle order off retains the adopted spare-blue cycler rule")
    {
        arm.Set(heurarm::CYCLONE_CYCLE_ORDER, 0).Set(heurarm::CYCLONE_CYCLER_RULE, 1);
        const GameState s = StateO({ kCycO, "Island", "Island" }, hand);
        CHECK(ProvO().SearchedCycleSources(s) == NamesO{ kCurO, kWraO, kCycO, kLoungeO });
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
    for (int b = 0; b < 3; ++b) { const int x = static_cast<int>((static_cast<std::uint32_t>(v) >> (8 * b)) & 0xFFu) - 1; if (x < 0) { break; } u.push_back(x); }
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
        const auto vs = SeedPaySplits(v, 0, two, DefO(kCycO));
        REQUIRE(vs.size() == 1);
        CHECK(SeedPaySplitHoldMask(v, 0, vs[0]) == (1ull << 2));
        const GameState w = StateO({ "Mountain", "Island", "Steam Vents" }, { kCycO, kCycO });
        const auto ws = SeedPaySplits(w, 0, two, DefO(kCycO));
        REQUIRE(ws.size() == 1);
        CHECK(SeedPaySplitHoldMask(w, 0, ws[0]) == (1ull << 2));   // only {Mountain, Island} is undominated
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

TEST_CASE("Seed payment split: partial float keeps the residual source choice searched")
{
    ArmO arm;
    arm.Set(heurarm::CYCLE_SEED_PAY_SPLIT, 1);
    GameState s = StateO({ "Mountain", "Mountain", "Island", "Island" }, { kCycO, kCycO });
    s.floating_mana.red = 1;
    const auto sp = SeedPaySplits(s, 0, DefO(kCycO).params.cycling_cost.value(), DefO(kCycO));
    REQUIRE(sp.size() == 5);   // RR/RU/UU with float kept, R/U with float spent
    for (const auto pin : sp)
    {
        CHECK(SplitUnitsO(pin).size() >= 1);
        GameState t = s;
        const auto hold = SeedPaySplitHoldMask(t, 0, pin);
        SeedPaySplitScope scope(t, 0, pin);
        REQUIRE(scope.Valid());
        REQUIRE(TapForCostShared(t, DefO(kCycO).params.cycling_cost.value(), false, nullptr, true));
        for (std::size_t i = 0; i < t.battlefield.size(); ++i)
        { CHECK(t.battlefield[i].tapped == ((hold & (1ull << i)) == 0)); }
    }
}

TEST_CASE("Seed payment split: filter and Myriad remain distinct special choices and each pin is realised")
{
    ArmO arm;
    arm.Set(heurarm::CYCLE_SEED_PAY_SPLIT, 1);
    GameState s = StateO({ "Cascade Bluffs", "Myriad Landscape", "Myriad Landscape", "Island", "Mountain" }, { kCycO, kCycO });
    ManaCost one;
    one.generic = 1;
    const auto sp = SeedPaySplits(s, 0, one, DefO(kCycO));
    REQUIRE(sp.size() >= 5);   // plus speculative filter float
    std::vector<std::uint64_t> holds;
    for (const auto pin : sp)
    {
        GameState t = s;
        const auto hold = SeedPaySplitHoldMask(t, 0, pin);
        SeedPaySplitScope scope(t, 0, pin);
        if (!scope.Valid()) { continue; }   // future filter float is not present yet
        holds.push_back(hold);
        REQUIRE(TapForCostShared(t, one, false, nullptr, true));
        for (std::size_t i = 0; i < t.battlefield.size(); ++i)
        { CHECK(t.battlefield[i].tapped == ((hold & (1ull << i)) == 0)); }
    }
    REQUIRE(holds.size() == 5);
    std::sort(holds.begin(), holds.end());
    CHECK(std::adjacent_find(holds.begin(), holds.end()) == holds.end());
}

TEST_CASE("Seed payment split: non-generic metadata and unsupported multi-unit sources do not claim exact splits")
{
    ArmO arm;
    arm.Set(heurarm::CYCLE_SEED_PAY_SPLIT, 1);
    const GameState s = StateO({ "Mountain", "Island", "Steam Vents" }, { kCycO });
    ManaCost cost;
    cost.generic = 2;
    SUBCASE("colourless pip") { cost.colorless = 1; }
    SUBCASE("snow pip") { cost.snow_pips = 1; }
    SUBCASE("twobrid") { cost.twobrid_count = 1; }
    SUBCASE("X") { cost.has_x = true; }
    CHECK(SeedPaySplits(s, 0, cost, DefO(kCycO)).empty());
    const GameState multi = StateO({ "Sol Ring", "Island", "Mountain" }, { kCycO });
    cost = ManaCost{};
    cost.generic = 2;
    CHECK(SeedPaySplits(multi, 0, cost, DefO(kCycO)).empty());
}

TEST_CASE("Seed payment split: keep red float while Islands pay the seed, preserving the held Cyclone RR")
{
    ArmO arm;
    arm.Set(heurarm::CYCLE_SEED_PAY_SPLIT, 1);
    GameState s = StateO({ "Mountain", "Island", "Island", "Island", "Island", "Island" }, { kCycO, kCycO });
    s.floating_mana.red = 1;
    const ManaCost cost = DefO(kCycO).params.cycling_cost.value();
    const auto pins = SeedPaySplits(s, 0, cost, DefO(kCycO));
    REQUIRE_FALSE(pins.empty());
    bool found = false;
    for (const auto pin : pins)
    {
        GameState t = s;
        {
            SeedPaySplitScope scope(t, 0, pin);
            if (!scope.Valid() || !TapForCostShared(t, cost, false, nullptr, true)) { continue; }
        }
        int untapped = 0;
        for (const Permanent& q : t.battlefield) { untapped += !q.tapped; }
        CHECK(untapped + t.floating_mana.Total() == 5);   // seven available units less the seed's two
        if (t.floating_mana.red == 1 && !t.battlefield[0].tapped
            && TapForCostShared(t, DefO(kCycO).card.m_mana_cost, false, nullptr, true)) { found = true; }
    }
    CHECK(found);
}

TEST_CASE("Seed payment split: invalid floating allocation is atomic and later ritual float is admitted")
{
    ArmO arm;
    arm.Set(heurarm::CYCLE_SEED_PAY_SPLIT, 1);
    GameState s = StateO({ "Mountain", "Island", "Island" }, { kCycO, kCycO, "Seething Song" });
    const auto pins = SeedPaySplits(s, 0, DefO(kCycO).params.cycling_cost.value(), DefO(kCycO));
    bool future_float_pin = false, paid_after_song = false;
    for (const auto pin : pins)
    {
        if ((static_cast<std::uint32_t>(pin) >> 24) == 0) { continue; }
        future_float_pin = true;
        GameState t = s;
        {
            SeedPaySplitScope scope(t, 0, pin);
            CHECK_FALSE(scope.Valid());
        }
        CHECK(t.floating_mana.Total() == 0);
        for (const auto& q : t.battlefield) { CHECK_FALSE(q.tapped); }
        // The same allocation becomes real after the plan's preceding Song has floated red.
        t.floating_mana.red = 5;
        for (auto& q : t.battlefield) { q.tapped = true; }   // Song consumed all three original lands
        {
            SeedPaySplitScope scope(t, 0, pin);
            CHECK(scope.Valid() == SplitUnitsO(pin).empty());
            if (!scope.Valid()) { continue; }
            CHECK(TapForCostShared(t, DefO(kCycO).params.cycling_cost.value(), false, nullptr, true));
            paid_after_song = true;
        }
        CHECK(t.floating_mana.red == 3);   // the spent two and unexposed three reunite correctly
    }
    CHECK(future_float_pin);
    CHECK(paid_after_song);
}

TEST_CASE("Seed payment split: a failed realised pin restores exposed float and every tap")
{
    ArmO arm;
    arm.Set(heurarm::CYCLE_SEED_PAY_SPLIT, 1);
    GameState s = StateO({ "Mountain", "Island", "Island", "Island" }, { kCycO, kCycO });
    s.floating_mana.red = 1;
    const auto pins = SeedPaySplits(s, 0, DefO(kCycO).params.cycling_cost.value(), DefO(kCycO));
    REQUIRE_FALSE(pins.empty());
    for (const auto pin : pins)
    {
        GameState t = s;
        {
            SeedPaySplitScope scope(t, 0, pin);
            REQUIRE(scope.Valid());
            ManaCost unaffordable;
            unaffordable.generic = 3;   // the pin selected exactly two units
            CHECK_FALSE(TapForCostShared(t, unaffordable, false, nullptr, true));
        }
        CHECK(t.floating_mana == s.floating_mana);
        for (const auto& q : t.battlefield) { CHECK_FALSE(q.tapped); }
    }
}

TEST_CASE("Cyclone Q64: middle creature-cycler tier differs from the before-all diagnostic")
{
    ArmO arm;
    arm.Set(heurarm::CYCLONE_CAST_ORDER, 1);
    arm.Set(heurarm::CYCLONE_CO_REV_CYCLERS, 0);
    arm.Set(heurarm::CYCLONE_CO_CYCLERS_BEFORE_SONG, 1);
    const GameState s = StateO({}, {});
    CHECK(ProvO().CastOrderRank(s, DefO(kCurO)) == 55);
    CHECK(ProvO().CastOrderRank(s, DefO(kWraO)) == 55);
    CHECK(ProvO().CastOrderRank(s, DefO("Remand")) < ProvO().CastOrderRank(s, DefO(kCurO)));
    CHECK(ProvO().CastOrderRank(s, DefO(kCurO)) < ProvO().CastOrderRank(s, DefO("Seething Song")));
    CHECK(ProvO().CastOrderTierName(55) != nullptr);
    arm.Set(heurarm::CYCLONE_CO_REV_CYCLERS, 1);
    CHECK(ProvO().CastOrderRank(s, DefO(kCurO)) == 5);
    arm.Set(heurarm::CYCLONE_CAST_ORDER, 0);
    CHECK(ProvO().CastOrderRank(s, DefO(kCurO)) == GenericProvider{}.CastOrderRank(s, DefO(kCurO)));
}

namespace
{
std::vector<Action> CastsInfoO(const NamesO& names)
{
    std::vector<Action> out;
    for (const auto& name : names)
    {
        Action a;
        a.kind = Action::Kind::CastFromHand;
        a.card_name = name;
        a.def = &DefO(name);
        out.push_back(a);
    }
    return out;
}
NamesO OrderNamesInfoO(const std::vector<Action>& acts, const std::vector<int>& order)
{
    NamesO out;
    for (int i : order) { out.push_back(acts[i].card_name.str()); }
    return out;
}
}

TEST_CASE("Cyclone Q60: DRAW searches independent information slots, canonical first")
{
    ArmO arm;
    arm.Set(heurarm::CYCLONE_CAST_ORDER, 1);
    arm.Set(heurarm::CYCLONE_ORDER_DRAW_TURNS, 1);
    const GameState s = StateO({ "Island", "Island", "Island", "Island", "Island", "Island", "Mountain", "Mountain", "Mountain" }, {});
    const auto acts = CastsInfoO({ "Seething Song", "Brainsurge", "Brainstorm", "Ponder", "Long-Term Plans" });
    std::vector<std::vector<int>> orders;
    REQUIRE(ProvO().WantsCastOrderingSearch());
    REQUIRE(ProvO().CastOrderingCandidates(s, acts, acts, orders));
    REQUIRE(orders.size() == 4);
    CHECK(OrderNamesInfoO(acts, orders[0]) == NamesO{ "Long-Term Plans", "Ponder", "Brainstorm", "Brainsurge", "Seething Song" });
    CHECK(OrderNamesInfoO(acts, orders[1]) == NamesO{ "Ponder", "Brainstorm", "Brainsurge", "Long-Term Plans", "Seething Song" });
    CHECK(OrderNamesInfoO(acts, orders[2]) == NamesO{ "Long-Term Plans", "Brainstorm", "Ponder", "Brainsurge", "Seething Song" });
    CHECK(OrderNamesInfoO(acts, orders[3]) == NamesO{ "Brainstorm", "Ponder", "Brainsurge", "Long-Term Plans", "Seething Song" });
    for (auto order : orders)
    {
        std::sort(order.begin(), order.end());
        CHECK(order == std::vector<int>{ 0, 1, 2, 3, 4 });
    }
    arm.Set(heurarm::CYCLONE_ORDER_DRAW_TURNS, 0);
    CHECK_FALSE(ProvO().WantsCastOrderingSearch());
    CHECK_FALSE(ProvO().CastOrderingCandidates(s, acts, acts, orders));
    arm.Set(heurarm::CYCLONE_ORDER_DRAW_TURNS, 1);
    arm.Set(heurarm::CYCLONE_CAST_ORDER, 0);
    CHECK_FALSE(ProvO().WantsCastOrderingSearch());
}

TEST_CASE("Cyclone Q60: each information pair emits only its two slots; no pair adds no probes")
{
    ArmO arm;
    arm.Set(heurarm::CYCLONE_CAST_ORDER, 1);
    arm.Set(heurarm::CYCLONE_ORDER_DRAW_TURNS, 1);
    const GameState s = StateO({ "Island", "Island", "Island", "Island", "Island", "Island" }, {});
    const auto check = [&](const NamesO& names, std::size_t count)
    {
        const auto acts = CastsInfoO(names);
        std::vector<std::vector<int>> orders;
        REQUIRE(ProvO().CastOrderingCandidates(s, acts, acts, orders));
        CHECK(orders.size() == count);
        for (auto order : orders)
        {
            std::sort(order.begin(), order.end());
            for (std::size_t i = 0; i < order.size(); ++i) { CHECK(order[i] == static_cast<int>(i)); }
        }
    };
    check({ "Long-Term Plans", "Brainsurge" }, 2);
    check({ "Ponder", "Brainstorm" }, 2);
    check({ "Ponder", "Brainstorm", "Brainstorm" }, 2);
    check({ "Ponder", "Brainsurge" }, 0);
    check({ "Remand", "Seething Song" }, 0);
}

TEST_CASE("Cyclone Q60: information slots preserve a required Song funding prefix")
{
    ArmO arm;
    arm.Set(heurarm::CYCLONE_CAST_ORDER, 1).Set(heurarm::CYCLONE_ORDER_DRAW_TURNS, 1);
    const GameState s = StateO({ "Mountain", "Mountain", "Mountain", "Mountain",
                                "Island", "Island", "Island" }, {});
    auto acts = CastsInfoO({ "Long-Term Plans", "Ponder", "Brainstorm", "Seething Song" });
    for (Action& a : acts)
    {
        a.cost = a.def->card.m_mana_cost;
        a.ritual_float = a.def->params.ritual_floating_mana;
    }
    std::vector<std::vector<int>> orders;
    REQUIRE(ProvO().CastOrderingCandidates(s, acts, acts, orders));
    REQUIRE(orders.size() == 4);
    for (const auto& order : orders)
    {
        // Three blue spells need every Island; Song must spend three Mountains
        // before LTP can consume two of them for generic. Moving information slots
        // after the funding ladder must not erase this required producer prefix.
        REQUIRE_FALSE(order.empty());
        CHECK(acts[order.front()].card_name == "Seething Song");
    }
}
