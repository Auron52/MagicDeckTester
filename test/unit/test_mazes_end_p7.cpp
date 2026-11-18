// Maze's End deck, P7 (2026-11-18; docs/design/analysis-Maze's End.md "P7"): the engine's CAST / LAND DOMINANCE
// FOLD (TurnSolver.cpp CastDominanceFold) under the Maze's End provider's premises (MazesEndProvider::
// CastDominanceLive / CastDominanceClass). A plan is dropped for its twin plus one more cast of Exploration /
// Burgeoning / Manabond / Amulet of Vigor (fold A), and a land-less plan of only such casts (Amulet aside) for its
// twin plus a land (fold B). Not live with a Treasure-making Gate untapped or in hand, or a Gateway Plaza in hand;
// Guild Summit vetoes. MTG_MAZE_CAST_DOMINANCE / MTG_MAZE_LAND_DOMINANCE, both DEFAULT ON.
// P7c: the off-main Maze's End keep needs a P6 window (MTG_MAZE_CASTDOM_WINDOWS); Heap Gate / Gateway Plaza are per-plan
// keeps (MTG_MAZE_CASTDOM_PERPLAN), not a blanket switch-off.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace
{
void LoadP7Cards()
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

void Put(GameState& s, const char* name, int number, bool tapped = false)
{
    Permanent p;
    p.card = C(name, number);
    p.controller_index = 0;
    p.owner_index = 0;
    p.tapped = tapped;
    s.battlefield.push_back(p);
}

struct Arm
{
    int slot;
    Arm(int sl, int v) : slot(sl) { heurarm::t_arm[sl] = static_cast<std::int8_t>(v); }
    ~Arm() { heurarm::t_arm[slot] = -1; }
};

// The turn-4 position of the census dump (seed 30001): Baldur's Gate, Amulet of Vigor, Golgari Guildgate in play;
// two Exploration, Burgeoning and four lands in hand.
GameState T4(const DecisionProvider& prov)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.m_provider          = &prov;
    s.deck_has_crossing_land_put = true;
    s.players[0].lands_played_this_turn = 0;
    Put(s, "Baldur's Gate", 1);
    Put(s, "Amulet of Vigor", 2);
    Put(s, "Golgari Guildgate", 3);
    s.players[0].hand.push_back(C("Exploration", 10));
    s.players[0].hand.push_back(C("Exploration", 11));
    s.players[0].hand.push_back(C("Gond Gate", 12));
    s.players[0].hand.push_back(C("Burgeoning", 13));
    s.players[0].hand.push_back(C("Manor Gate", 14));
    s.players[0].hand.push_back(C("Maze's End", 15));
    for (int k = 0; k < 8; ++k) { s.players[0].library.push_back(C(k % 2 ? "Izzet Guildgate" : "Sylvan Scrying", 30 + k)); }
    return s;
}

// Each base plan (bp_choice < 0) as "land|cast,cast" (cast names in plan order).
std::set<std::string> Menu(const GameState& s)
{
    std::set<std::string> out;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        if (p.bp_choice >= 0) { continue; }
        std::string k = p.land_to_play + "|";
        for (const Action& a : p.actions) { k += a.card_name.str() + ","; }
        out.insert(k);
    }
    return out;
}
}   // namespace

TEST_CASE("Maze's End P7: the cast / land dominance fold drops plans a sibling dominates")
{
    LoadP7Cards();
    const MazesEndProvider prov;
    const GameState s = T4(prov);
    CHECK(prov.CastDominanceLive(s));
    using DP = DecisionProvider;
    CHECK(prov.CastDominanceClass(s, D("Exploration")) == (DP::kCastDomAdd | DP::kCastDomLandOk | DP::kCastDomOpensBp));
    CHECK(prov.CastDominanceClass(s, D("Burgeoning")) == (DP::kCastDomAdd | DP::kCastDomLandOk));
    CHECK(prov.CastDominanceClass(s, D("Manabond")) == (DP::kCastDomAdd | DP::kCastDomLandOk));
    CHECK(prov.CastDominanceClass(s, D("Amulet of Vigor")) == DP::kCastDomAdd);
    CHECK(prov.CastDominanceClass(s, D("Guild Summit")) == -1);
    CHECK(prov.CastDominanceClass(s, D("Sylvan Scrying")) == 0);
    CHECK(prov.CastDominanceClass(s, D("Gond Gate")) == 0);

    std::set<std::string> off_menu;
    {
        const Arm off(heurarm::MAZE_CAST_DOMINANCE, 0);
        CHECK_FALSE(prov.CastDominanceLive(s));
        off_menu = Menu(s);
    }
    const std::set<std::string> on_menu = Menu(s);
    // Restrict-only: every plan the fold keeps is on the unfolded menu, and it removes something here.
    for (const std::string& k : on_menu) { CHECK(off_menu.count(k) == 1); }
    CHECK(on_menu.size() < off_menu.size());
    // The unfolded menu holds the dominated shapes; the folded one does not.
    CHECK(off_menu.count("|") == 1);                    // pass
    CHECK(off_menu.count("Manor Gate|") == 1);          // a land alone, while "+ Exploration" is affordable
    CHECK(on_menu.count("|") == 0);
    CHECK(on_menu.count("Manor Gate|") == 0);
    CHECK(on_menu.count("Gond Gate|") == 0);
    // A plan that opens a breakpoint is never dropped by fold A: "Manor Gate + Exploration" stays beside "Manor Gate +
    // Exploration + Exploration" (its continuation may spend the mana the second cast would take).
    if (off_menu.count("Manor Gate|Exploration,") == 1) { CHECK(on_menu.count("Manor Gate|Exploration,") == 1); }
    // A land-less plan of land-ok casts goes for its land twin (fold B) ...
    if (off_menu.count("|Exploration,") == 1) { CHECK(on_menu.count("|Exploration,") == 0); }
    {
        // ... and stays with fold B off (fold A alone), as does every maximal plan.
        const Arm b_off(heurarm::MAZE_LAND_DOMINANCE, 0);
        const std::set<std::string> a_menu = Menu(s);
        if (off_menu.count("|Exploration,") == 1) { CHECK(a_menu.count("|Exploration,") == 1); }
        for (const std::string& k : on_menu) { CHECK(a_menu.count(k) == 1); }
    }
    // Every maximal plan of the unfolded menu survives: the best two-cast lines are all still there.
    for (const std::string& k : off_menu)
    {
        if (k.find("Exploration,Exploration,") != std::string::npos || k.find("Exploration,Burgeoning,") != std::string::npos)
        { CHECK(on_menu.count(k) == 1); }
    }
}

TEST_CASE("Maze's End P7: the fold's premise -- not live with a Treasure Gate, a Gateway Plaza or a Guild Summit")
{
    LoadP7Cards();
    const MazesEndProvider prov;
    // The blanket switch-off of P7 (MTG_MAZE_CASTDOM_PERPLAN=0); P7c (b) replaces it with per-plan keeps (below).
    const Arm pp_off(heurarm::MAZE_CASTDOM_PERPLAN, 0);
    {
        GameState s = T4(prov);
        Put(s, "Heap Gate", 40);                        // untapped: the last-chance Treasure could use held mana
        CHECK_FALSE(prov.CastDominanceLive(s));
    }
    {
        GameState s = T4(prov);
        Put(s, "Heap Gate", 40, /*tapped=*/true);       // tapped: it cannot make a Treasure before our untap
        CHECK(prov.CastDominanceLive(s));
    }
    {
        GameState s = T4(prov);
        s.players[0].hand.push_back(C("Heap Gate", 41));
        CHECK_FALSE(prov.CastDominanceLive(s));
    }
    {
        GameState s = T4(prov);
        s.players[0].hand.push_back(C("Gateway Plaza", 42));   // Burgeoning / Manabond could put it off-main
        CHECK_FALSE(prov.CastDominanceLive(s));
    }
    {
        GameState s = T4(prov);
        Put(s, "Guild Summit", 43);                     // a Gate entering draws: every land play is a breakpoint
        CHECK_FALSE(prov.CastDominanceLive(s));
    }
}

TEST_CASE("Maze's End P7 x P6: a plan whose unspent mana pays an off-main Maze's End activation is kept")
{
    LoadP7Cards();
    const MazesEndProvider prov;
    // The P7b guard as measured (mana alone, MTG_MAZE_CASTDOM_WINDOWS=0); P7c (a) narrows it to P6's windows (below).
    const Arm w_off(heurarm::MAZE_CASTDOM_WINDOWS, 0);
    using PV = DecisionProvider::PlanView;
    const std::vector<Action> none;
    const std::string manor = "Manor Gate", no_land;
    {
        // T4: Baldur's Gate + Golgari Guildgate untapped, one land drop. U("Manor Gate", no casts) = 2 + 1 (the drop)
        // - 0 - 1 (Maze's End taps itself) = 2 < 3: no off-main activation affordable, the fold may drop it.
        const GameState s = T4(prov);
        CHECK_FALSE(prov.CastDominanceKeep(s, PV{ &none, &manor }));
        CHECK_FALSE(prov.CastDominanceKeep(s, PV{ &none, &no_land }));
        const std::set<std::string> on_menu = Menu(s);
        CHECK(on_menu.count("Manor Gate|") == 0);
        CHECK(on_menu.count("|") == 0);
    }
    {
        // Two more untapped Gates: U = 4 + 1 - 0 - 1 = 4 >= 3. The land alone and the pass now leave the {3} that an
        // end-step / opponent's-turn activation of Maze's End (held in hand, played or put later) could use, so the
        // fold keeps them -- with P6 on; with P6's levers off the old premise holds and they go.
        GameState s = T4(prov);
        Put(s, "Izzet Guildgate", 20);
        Put(s, "Dimir Guildgate", 21);
        CHECK(prov.CastDominanceLive(s));
        CHECK(prov.CastDominanceKeep(s, PV{ &none, &manor }));
        CHECK(prov.CastDominanceKeep(s, PV{ &none, &no_land }));
        std::set<std::string> off_menu;
        {
            const Arm off(heurarm::MAZE_CAST_DOMINANCE, 0);
            off_menu = Menu(s);
        }
        const std::set<std::string> on_menu = Menu(s);
        for (const std::string& k : on_menu) { CHECK(off_menu.count(k) == 1); }
        REQUIRE(off_menu.count("Manor Gate|") == 1);
        CHECK(on_menu.count("Manor Gate|") == 1);
        CHECK(on_menu.count("|") == 1);
        {
            const Arm r_off(heurarm::MAZE_OFFMAIN_RESPOND, 0);
            const Arm b_off(heurarm::MAZE_OFFMAIN_BANK, 0);
            CHECK_FALSE(prov.CastDominanceKeep(s, PV{ &none, &manor }));
            const std::set<std::string> p5_menu = Menu(s);
            CHECK(p5_menu.count("Manor Gate|") == 0);
            CHECK(p5_menu.count("|") == 0);
        }
    }
    {
        // The same mana but no Maze's End reachable (none in play, none in hand): nothing off-main can use it.
        GameState s = T4(prov);
        Put(s, "Izzet Guildgate", 20);
        Put(s, "Dimir Guildgate", 21);
        auto& h = s.players[0].hand;
        h.erase(std::remove_if(h.begin(), h.end(), [](const Card& c) { return c.m_name.str() == "Maze's End"; }), h.end());
        CHECK_FALSE(prov.CastDominanceKeep(s, PV{ &none, &manor }));
        CHECK(Menu(s).count("Manor Gate|") == 0);
    }
    {
        // A tapped Maze's End in play cannot be activated before our untap; one untapped can. 3 untapped other sources:
        // U(no land, no casts) = 3 (+ Maze's End's own {C}) + 1 (the drop) - 1 = 4 with it untapped.
        GameState s = T4(prov);
        auto& h = s.players[0].hand;
        h.erase(std::remove_if(h.begin(), h.end(), [](const Card& c) { return c.m_name.str() == "Maze's End"; }), h.end());
        Put(s, "Izzet Guildgate", 20);
        Put(s, "Maze's End", 22, /*tapped=*/true);
        CHECK_FALSE(prov.CastDominanceKeep(s, PV{ &none, &no_land }));
        s.battlefield.back().tapped = false;
        CHECK(prov.CastDominanceKeep(s, PV{ &none, &no_land }));
    }
}

TEST_CASE("Maze's End P7c (a): the off-main Maze's End keep needs a P6 window and a fetchable Gate")
{
    LoadP7Cards();
    const MazesEndProvider prov;
    using PV = DecisionProvider::PlanView;
    const std::vector<Action> none;
    const std::string manor = "Manor Gate", no_land;
    GameState s = T4(prov);
    Put(s, "Izzet Guildgate", 20);
    Put(s, "Dimir Guildgate", 21);
    // U0 - 1 = 4 + 1 - 1 = 4 >= 3, Maze's End in hand -- but no Manabond and no Burgeoning controlled: P6 offers the
    // activation in no window, so the unspent mana has no off-main use and the plan may go.
    CHECK_FALSE(prov.CastDominanceKeep(s, PV{ &none, &manor }));
    CHECK_FALSE(prov.CastDominanceKeep(s, PV{ &none, &no_land }));
    {
        const Arm w_off(heurarm::MAZE_CASTDOM_WINDOWS, 0);   // the P7b guard: kept on mana alone
        CHECK(prov.CastDominanceKeep(s, PV{ &none, &manor }));
    }
    // A Burgeoning in play opens a window only while the opponent plays lands.
    {
        GameState b = s;
        Put(b, "Burgeoning", 23);
        CHECK_FALSE(prov.CastDominanceKeep(b, PV{ &none, &manor }));
        b.opponent_plays_lands = true;
        CHECK(prov.CastDominanceKeep(b, PV{ &none, &manor }));
        // ... and needs a Gate Maze's End could fetch.
        for (Card& c : b.players[0].library) { if (c.m_name.str() == "Izzet Guildgate") { c = C("Sylvan Scrying", c.m_number); } }
        CHECK_FALSE(prov.CastDominanceKeep(b, PV{ &none, &manor }));
    }
    // A Manabond in play (or cast by P) is a window.
    {
        GameState m = s;
        Put(m, "Manabond", 24);
        CHECK(prov.CastDominanceKeep(m, PV{ &none, &manor }));
    }
    {
        Action mb;
        mb.kind = Action::Kind::CastFromHand;
        mb.def = &D("Manabond");
        mb.card_name = mb.def->card.m_name;
        const std::vector<Action> cast_mb{ mb };
        GameState m = s;
        Put(m, "Golgari Guildgate", 25);   // one more source: U0 - 1 = 5 + 1 - 1 (Manabond) - 1 = 4 >= 3
        CHECK(prov.CastDominanceKeep(m, PV{ &cast_mb, &manor }));
    }
    // A HELD Manabond counts only when P opens a breakpoint (an Exploration cast): its continuation may cast it.
    {
        GameState m = s;
        Put(m, "Golgari Guildgate", 25);
        m.players[0].hand.push_back(C("Manabond", 26));
        CHECK_FALSE(prov.CastDominanceKeep(m, PV{ &none, &manor }));
        Action ex;
        ex.kind = Action::Kind::CastFromHand;
        ex.def = &D("Exploration");
        ex.card_name = ex.def->card.m_name;
        const std::vector<Action> cast_ex{ ex };
        CHECK(prov.CastDominanceKeep(m, PV{ &cast_ex, &no_land }));
    }
}

TEST_CASE("Maze's End P7c (b): Heap Gate and Gateway Plaza are per-plan keeps, not a blanket switch-off")
{
    LoadP7Cards();
    const MazesEndProvider prov;
    using PV = DecisionProvider::PlanView;
    const std::vector<Action> none;
    const std::string manor = "Manor Gate", no_land;
    auto no_maze = [](GameState& g)
    {
        auto& h = g.players[0].hand;
        h.erase(std::remove_if(h.begin(), h.end(), [](const Card& c) { return c.m_name.str() == "Maze's End"; }), h.end());
    };
    {
        // An untapped Heap Gate: the fold stays live; a plan is kept only when its leftover pays the Treasure's {1}
        // besides the Gate's own tap.
        GameState s = T4(prov);
        no_maze(s);
        Put(s, "Heap Gate", 40);
        CHECK(prov.CastDominanceLive(s));
        {
            const Arm pp_off(heurarm::MAZE_CASTDOM_PERPLAN, 0);   // the blanket switch-off it replaces
            CHECK_FALSE(prov.CastDominanceLive(s));
        }
        // U0 = 3 sources + 1 drop = 4: "Manor Gate" alone leaves 4 - 1 >= 1 -> kept.
        CHECK(prov.CastDominanceKeep(s, PV{ &none, &manor }));
        // A plan spending everything cannot fund it: five Amulet casts ({1} each, no extra drop) -> U0 <= 4 + 1 - 5 = 0.
        Action am;
        am.kind = Action::Kind::CastFromHand;
        am.def = &D("Amulet of Vigor");
        am.card_name = am.def->card.m_name;
        const std::vector<Action> four{ am, am, am, am, am };
        CHECK_FALSE(prov.CastDominanceKeep(s, PV{ &four, &manor }));
        s.battlefield.back().tapped = true;   // a tapped Heap Gate cannot make a Treasure before our untap
        CHECK_FALSE(prov.CastDominanceKeep(s, PV{ &none, &manor }));
    }
    {
        // A Heap Gate in hand: live, and kept on the same bound (it may enter untapped).
        GameState s = T4(prov);
        no_maze(s);
        s.players[0].hand.push_back(C("Heap Gate", 41));
        CHECK(prov.CastDominanceLive(s));
        CHECK(prov.CastDominanceKeep(s, PV{ &none, &manor }));
    }
    {
        // A Gateway Plaza in hand: live; kept only with a put window (Manabond, or Burgeoning while the opponent
        // plays lands) and {1} left for its ETB.
        GameState s = T4(prov);
        no_maze(s);
        s.players[0].hand.push_back(C("Gateway Plaza", 42));
        CHECK(prov.CastDominanceLive(s));
        {
            const Arm pp_off(heurarm::MAZE_CASTDOM_PERPLAN, 0);
            CHECK_FALSE(prov.CastDominanceLive(s));
        }
        CHECK_FALSE(prov.CastDominanceKeep(s, PV{ &none, &manor }));   // Burgeoning held, no window
        Put(s, "Manabond", 43);
        CHECK(prov.CastDominanceKeep(s, PV{ &none, &manor }));
    }
}
