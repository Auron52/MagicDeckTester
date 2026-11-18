// Maze's End P5b (2026-11-18; docs/design/analysis-Maze's End.md, section P5b): search identity folds.
//
// Fold 3 -- Archdruid's Charm's tutor axis emits the BASE plan (tutor_choice -1: the provider's front land,
// UNPINNED entry) plus one variant per (index, land-entry pin), index 0 included. The c==0 pinned variant is
// an identity fold only if the unpinned default colour always equals LandEntryPinCandidates().front(). It does
// NOT: LockEtbChosenColor asks the provider's EtbChosenColor (hand demand vs supply) FIRST, and only falls back
// to the pin menu's front when nothing is demanded. This test pins that finding, so the c==0 variant is not
// folded (it is a genuinely different colour whenever the hand demands one the P1 rule does not pick).
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{
void LoadP5bCards()
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

GameState Base(const DecisionProvider* prov)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.m_provider          = prov;
    // The list's spenders, so the provider's spendable-colour premise holds (G, U, R pips).
    s.players[0].library.push_back(C("Guild Summit", 100));
    s.players[0].library.push_back(C("Sylvan Scrying", 101));
    s.players[0].library.push_back(C("Gates Ablaze", 102));
    return s;
}

// The colour an UNPINNED entry locks (the Charm's base plan: g_land_entry_pin -1).
int UnpinnedColour(GameState s, const char* land)
{
    Permanent p;
    p.card = C(land, 200);
    p.controller_index = 0;
    p.owner_index = 0;
    p.tapped = true;
    s.battlefield.push_back(p);
    LockEtbChosenColor(s, 0, static_cast<int>(s.battlefield.size()) - 1, -1);
    return s.battlefield.back().chosen_color;
}
}   // namespace

TEST_CASE("Maze's End P5b fold 3: the Charm's unpinned default is NOT always pins.front() -- no identity fold")
{
    LoadP5bCards();
    const MazesEndProvider prov;
    const CardDefinition& cliff = D("Cliffgate");

    // No coloured demand in hand: EtbChosenColor returns -1, the default falls to the pin menu's front. EQUAL.
    {
        GameState s = Base(&prov);
        s.players[0].hand.push_back(C("Sea Gate", 10));   // a land: demands nothing
        const std::vector<int> pins = prov.LandEntryPinCandidates(s, 0, cliff);
        REQUIRE(pins.size() == 1);
        CHECK(pins.front() == static_cast<int>(Color::Green));   // P1: no green source -> G
        CHECK(UnpinnedColour(s, "Cliffgate") == pins.front());
    }
    // A blue spell in hand and no blue source: the demand rule picks U, the P1 menu still says G. DIFFERENT --
    // the c==0 pinned variant (G) is a distinct line from the base (U), so folding it would delete a colour.
    {
        GameState s = Base(&prov);
        s.players[0].hand.push_back(C("Guild Summit", 11));   // {2}{U}
        const std::vector<int> pins = prov.LandEntryPinCandidates(s, 0, cliff);
        REQUIRE(pins.size() == 1);
        CHECK(pins.front() == static_cast<int>(Color::Green));
        CHECK(UnpinnedColour(s, "Cliffgate") == static_cast<int>(Color::Blue));
        CHECK(UnpinnedColour(s, "Cliffgate") != pins.front());
    }
}

// ---- Fold 2: the extra-drop land order (MTG_FOLD_LAND_ORDER) -------------------------------------------------
namespace
{
struct ArmP5b
{
    int slot;
    ArmP5b(int sl, int v) : slot(sl) { heurarm::t_arm[sl] = static_cast<std::int8_t>(v); }
    ~ArmP5b() { heurarm::t_arm[slot] = -1; }
};

// Exploration out; `first` was PLAYED this turn (held since main 1); `hand` waits for the extra drop.
GameState AfterOneDrop(const DecisionProvider* prov, const char* first, const std::vector<const char*>& hand)
{
    GameState s = Base(prov);
    s.deck_has_landfall = false;   // StampDeckTraits clears it for Maze's End (no landfall in the list)
    Permanent ex;
    ex.card = C("Exploration", 1);
    ex.controller_index = 0; ex.owner_index = 0;
    s.battlefield.push_back(ex);
    s.players[0].hand.push_back(C(first, 2));
    int n = 3;
    for (const char* h : hand) { s.players[0].hand.push_back(C(h, n++)); }
    s.StampMain1Hand();
    // ...then the section played `first` (it entered tapped: no Amulet).
    s.players[0].hand.erase(s.players[0].hand.begin());
    Permanent land;
    land.card = C(first, 2);
    land.controller_index = 0; land.owner_index = 0;
    land.tapped = true;
    land.entered_this_turn = true;
    s.battlefield.push_back(land);
    s.players[0].lands_played_this_turn = 1;
    s.players[0].bonus_land_drops_this_turn = StaticExtraLandDrops(s, 0);
    REQUIRE(s.players[0].LandDropsAvailable() == 2);
    return s;
}

int LandFirstPlans(const std::vector<TurnSolver::Plan>& plans, const char* land)
{
    int n = 0;
    for (const TurnSolver::Plan& p : plans) { if (p.land_to_play == land && p.land_slot == 0) { ++n; } }
    return n;
}

int LandFirstPlans(const GameState& s, const char* land, int fold)
{
    const ArmP5b arm(heurarm::FOLD_LAND_ORDER, fold);
    return LandFirstPlans(TurnSolver::EnumerateMainPlans(s, true), land);
}
}   // namespace

TEST_CASE("Maze's End P5b fold 2: after a land-only section, exactly ONE order of two commuting lands is offered")
{
    LoadP5bCards();
    const MazesEndProvider prov;
    // Azorius first, Boros second -- and the mirror. Both orders reach one board; the fold keeps exactly one.
    const GameState ab = AfterOneDrop(&prov, "Azorius Guildgate", { "Boros Guildgate" });
    const GameState ba = AfterOneDrop(&prov, "Boros Guildgate", { "Azorius Guildgate" });
    REQUIRE(LandFirstPlans(ab, "Boros Guildgate", 0) > 0);
    REQUIRE(LandFirstPlans(ba, "Azorius Guildgate", 0) > 0);
    const bool ab_kept = LandFirstPlans(ab, "Boros Guildgate", 1) > 0;
    const bool ba_kept = LandFirstPlans(ba, "Azorius Guildgate", 1) > 0;
    CHECK(ab_kept != ba_kept);
    // The kept order is untouched (no plan of it removed).
    if (ab_kept) { CHECK(LandFirstPlans(ab, "Boros Guildgate", 1) == LandFirstPlans(ab, "Boros Guildgate", 0)); }
    else         { CHECK(LandFirstPlans(ba, "Azorius Guildgate", 1) == LandFirstPlans(ba, "Azorius Guildgate", 0)); }
    // The fold never touches the no-land plans.
    const GameState& dropped = ab_kept ? ba : ab;
    const ArmP5b on(heurarm::FOLD_LAND_ORDER, 1);
    int no_land = 0;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(dropped, true)) { if (p.land_to_play.empty()) { ++no_land; } }
    CHECK(no_land > 0);
}

TEST_CASE("Maze's End P5b fold 2: it never folds an order-sensitive land, a drawn land, or a turn that did more than play lands")
{
    LoadP5bCards();
    const MazesEndProvider prov;
    // Find the dropped direction first (canonical order is the land signature, not the name).
    const GameState ab = AfterOneDrop(&prov, "Azorius Guildgate", { "Boros Guildgate" });
    const bool boros_dropped_after_azorius = LandFirstPlans(ab, "Boros Guildgate", 1) == 0;
    const char* first  = boros_dropped_after_azorius ? "Azorius Guildgate" : "Boros Guildgate";
    const char* second = boros_dropped_after_azorius ? "Boros Guildgate" : "Azorius Guildgate";
    {
        const GameState s = AfterOneDrop(&prov, first, { second });
        CHECK(LandFirstPlans(s, second, 1) == 0);   // the folded direction
    }
    // A colour-choosing Gate in hand is order-sensitive (P1's colour reads the sources already down): kept.
    {
        const GameState s = AfterOneDrop(&prov, first, { "Cliffgate" });
        CHECK(LandFirstPlans(s, "Cliffgate", 1) == LandFirstPlans(s, "Cliffgate", 0));
        CHECK(LandFirstPlans(s, "Cliffgate", 1) > 0);
    }
    // The second land was DRAWN this turn (not held since main 1): its twin order may not exist. Kept.
    {
        GameState s = AfterOneDrop(&prov, first, {});
        s.players[0].hand.push_back(C(second, 40));
        CHECK(LandFirstPlans(s, second, 1) == LandFirstPlans(s, second, 0));
        CHECK(LandFirstPlans(s, second, 1) > 0);
    }
    // Something other than a land drop happened (a spell was cast): kept.
    {
        GameState s = AfterOneDrop(&prov, first, { second });
        s.spells_cast_this_turn = 1;
        CHECK(LandFirstPlans(s, second, 1) == LandFirstPlans(s, second, 0));
    }
    // A permanent that did not enter this turn is tapped (it paid for something): kept.
    {
        GameState s = AfterOneDrop(&prov, first, { second });
        s.battlefield.front().tapped = true;   // Exploration (tapped for a cost, as a test of the rule)
        CHECK(LandFirstPlans(s, second, 1) == LandFirstPlans(s, second, 0));
    }
    // Two Amulets float mana coloured by the line at that moment: every land is order-sensitive. Kept.
    {
        GameState s = AfterOneDrop(&prov, first, { second });
        for (int k = 0; k < 2; ++k)
        {
            Permanent am;
            am.card = C("Amulet of Vigor", 50 + k);
            am.controller_index = 0; am.owner_index = 0;
            s.battlefield.insert(s.battlefield.begin(), am);
        }
        CHECK(LandFirstPlans(s, second, 1) == LandFirstPlans(s, second, 0));
    }
    // Guild Summit out: a Gate and a non-Gate (Maze's End) draw differently on entry -> different classes. Pick
    // the direction the fold takes WITHOUT a Summit, then show the Summit keeps it.
    {
        const GameState g_m = AfterOneDrop(&prov, "Azorius Guildgate", { "Maze's End" });
        const GameState m_g = AfterOneDrop(&prov, "Maze's End", { "Azorius Guildgate" });
        const bool maze_dropped = LandFirstPlans(g_m, "Maze's End", 1) == 0;
        const bool gate_dropped = LandFirstPlans(m_g, "Azorius Guildgate", 1) == 0;
        REQUIRE(maze_dropped != gate_dropped);   // no Summit: one direction folds
        GameState s = maze_dropped ? g_m : m_g;
        const char* held = maze_dropped ? "Maze's End" : "Azorius Guildgate";
        Permanent gs;
        gs.card = C("Guild Summit", 55);
        gs.controller_index = 0; gs.owner_index = 0;
        s.battlefield.insert(s.battlefield.begin(), gs);
        CHECK(LandFirstPlans(s, held, 1) == LandFirstPlans(s, held, 0));
        CHECK(LandFirstPlans(s, held, 1) > 0);
    }
}
