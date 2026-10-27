// The MANA COMPLETENESS ORACLE's mana table (manaoracle::MinimalPayments; docs/design/mana-completeness-oracle.md).
//
// The oracle's whole claim is that its payment enumeration is COMPLETE: every way to pay is listed. Its
// runtime self-check (I1, "the engine's own outcome is among the oracle's") catches a gap only on a board
// some game happens to reach, so the shapes that once escaped it are pinned here: a land Aura's bonus on a
// painland's painless {C} face, a fed filter whose feeder is also a payer, Generator Servant's
// haste-tagged {C} kept floating, and an optimistic cover whose real payment fails.
#include <doctest/doctest.h>

#include "ai/ManaOracle.h"
#include "ai/ManaPayment.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
void EnsureOracleCards()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

// A board of named permanents with DISTINCT ids (the oracle names each tap by Card::m_number).
GameState Board(const std::vector<std::string>& names)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    int num = 1;
    for (const std::string& n : names)
    {
        const CardDefinition* def = CardDatabase::Instance().Lookup(n);
        REQUIRE_MESSAGE(def != nullptr, "card not in cards.json: ", n);
        Permanent p;
        p.card              = def->card;
        p.card.m_number     = num++;
        p.controller_index  = 0;
        p.owner_index       = 0;
        s.battlefield.push_back(p);
    }
    return s;
}

ManaCost Cost(int generic, int w = 0, int u = 0, int b = 0, int r = 0, int g = 0)
{
    ManaCost c;
    c.generic = generic; c.white = w; c.blue = u; c.black = b; c.red = r; c.green = g;
    return c;
}

// "Name:FACE" for every tap of an option, sorted -- an order-free description to assert on.
std::vector<std::string> Taps(const manaoracle::PayOption& o)
{
    static const char kL[] = "WUBRGC";
    std::vector<std::string> v;
    for (const TurnSolver::PreTap& t : o.taps)
    {
        std::string f;
        if (!t.combo.empty()) { for (int c : t.combo) { f.push_back(kL[c]); } }
        else if (t.color >= 0) { f.push_back(kL[t.color]); }
        v.push_back(t.name + ":" + f);
    }
    std::sort(v.begin(), v.end());
    return v;
}

bool HasOption(const std::vector<manaoracle::PayOption>& opts, std::vector<std::string> want)
{
    std::sort(want.begin(), want.end());
    for (const manaoracle::PayOption& o : opts) { if (Taps(o) == want) { return true; } }
    return false;
}
} // namespace

TEST_CASE("mana oracle: a painland pays a {G} land Aura cast painlessly through its {C} face + the Aura's {G}")
{
    EnsureOracleCards();
    // Brushland enchanted by Wild Growth: {T}: {C} is painless, and Wild Growth adds {G}{G} on ANY tap
    // for mana -- so {G} is payable without the land's 1 damage. The EDF ORACLE-GAP of 2026-10-26
    // was this option missing (the Aura host was reserved by the engine's heuristic).
    GameState s = Board({ "Brushland", "Wild Growth" });
    s.battlefield[1].aura_attached_to = s.battlefield[0].card.m_number;
    const auto opts = manaoracle::MinimalPayments(s, Cost(0, 0, 0, 0, 0, 1), /*for_creature=*/false, nullptr);
    CHECK(HasOption(opts, { "Brushland:C" }));
}

TEST_CASE("mana oracle: a fed filter whose feeder also pays is offered (the converter cannot be fed later)")
{
    EnsureOracleCards();
    // Island + Cascade Bluffs paying {U}: Island alone pays, but "Island feeds Bluffs -> {U}{U}" pays AND
    // floats a {U} -- and once the Island is tapped Bluffs has no feeder, so it is not "the same as tapping
    // Bluffs later". The NivMizzetCombo Ponder/Preordain ORACLE-GAP was this option missing.
    const GameState s = Board({ "Island", "Cascade Bluffs" });
    const auto opts = manaoracle::MinimalPayments(s, Cost(0, 0, 1), false, nullptr);
    CHECK(HasOption(opts, { "Island:U" }));
    CHECK(HasOption(opts, { "Island:U", "Cascade Bluffs:UU" }));
}

TEST_CASE("mana oracle: Generator Servant's haste-tagged {C} can be kept floating for a later creature spell")
{
    EnsureOracleCards();
    // Two haste-tagged {C} floating (a cracked Servant) and an Island; a noncreature {1}. Spending a
    // tagged {C} here costs a later creature its haste, so paying with the Island while BOTH stay
    // floating must be one of the options -- the NivMizzetCombo DOMINATED-HASTE shape.
    GameState s = Board({ "Island" });
    s.floating_mana.colorless = 2;
    s.floating_haste_c        = 2;
    const auto opts = manaoracle::MinimalPayments(s, Cost(1), false, nullptr);
    bool kept_both = false;
    for (const manaoracle::PayOption& o : opts)
    { if (Taps(o) == std::vector<std::string>{ "Island:U" } && o.stash_haste == 2) { kept_both = true; } }
    CHECK(kept_both);
    // ...and simply spending the float is one too.
    CHECK(HasOption(opts, {}));
}

TEST_CASE("mana oracle: every listed option really pays (verified), and none is listed twice")
{
    EnsureOracleCards();
    const GameState s = Board({ "Forest", "Forest", "Island", "Brushland" });
    const ManaCost c = Cost(1, 0, 0, 0, 0, 1);
    const auto opts = manaoracle::MinimalPayments(s, c, false, nullptr);
    REQUIRE(!opts.empty());
    for (const manaoracle::PayOption& o : opts)
    {
        GameState sc = s;
        for (const TurnSolver::PreTap& t : o.taps) { REQUIRE(ApplyHumanPreTap(sc, t).empty()); }
        manaoracle::t_float_only = true;
        const bool paid = TapForCostShared(sc, c, false, nullptr, true);
        manaoracle::t_float_only = false;
        CHECK(paid);
    }
    // The two Forests are one class: "Forest + Island" appears once, not once per Forest.
    int forest_island = 0;
    for (const manaoracle::PayOption& o : opts)
    { if (Taps(o) == std::vector<std::string>{ "Forest:G", "Island:U" }) { ++forest_island; } }
    CHECK(forest_island == 1);
}

// KNOWN ENGINE BUG (2026-10-27, found by the oracle; docs/design/mana-completeness-oracle.md): on an
// UNARMED board TapSourceIntoFloat treats every {C} tap of a land with a {C} mode as a painland's
// painless ability and never reads tap_self_damage_any_mode, so the payment and the pre-tap tap Ancient
// Tomb for free (a real Fungus game: tapped, life stays 20) while the search's plan application charges
// it -- the source of every Fungus DOMINATED-LIFE "+2 life". should_fail until the fix lands; then
// drop the decorator.
TEST_CASE("mana oracle: a scripted Ancient Tomb tap deals its 2 damage, as the engine's own payment does"
          * doctest::should_fail(true))
{
    EnsureOracleCards();
    // Ancient Tomb hurts in EVERY mode (tap_self_damage_any_mode): its {C}{C} is not a painland's
    // separate painless {C}. A pre-tap that skipped the damage would make every oracle payment through
    // the Tomb look 2 life better than the engine's, i.e. a false DOMINATED-LIFE.
    GameState eng = Board({ "Ancient Tomb" });
    REQUIRE(TapForCostShared(eng, Cost(2), false, nullptr, true));
    GameState pre = Board({ "Ancient Tomb" });
    const TurnSolver::PreTap t{ "Ancient Tomb", 1, static_cast<int>(Color::Colorless) };
    REQUIRE(ApplyHumanPreTap(pre, t).empty());
    CHECK(eng.players[0].life == 18);
    CHECK(pre.players[0].life == eng.players[0].life);
}
