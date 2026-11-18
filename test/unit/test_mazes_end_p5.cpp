// Maze's End deck, P5 (2026-11-18; docs/design/analysis-Maze's End.md "P5"): the USER-directed provider prunes of
// the P4 candidate list and the B disclosures -- Maze's End's fail-to-find, Gateway Plaza pay-or-don't-play,
// Heap Gate's Treasure only where its mana is otherwise unused, Manabond's "no" with an all-land hand, the v5
// fetch weight, and the two A/B variants (one colour alternating; Talon only while lacking extra drops). Each lever
// is DEFAULT ON (variants OFF); each test also pins it the other way.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
void LoadP5Cards()
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

GameState Base(const DecisionProvider& prov)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.m_provider          = &prov;
    s.deck_has_crossing_land_put = true;
    return s;
}

struct Arm
{
    int slot;
    Arm(int sl, int v) : slot(sl) { heurarm::t_arm[sl] = static_cast<std::int8_t>(v); }
    ~Arm() { heurarm::t_arm[slot] = -1; }
};

Action Cast(const char* name, int hand_index)
{
    Action a;
    a.kind = Action::Kind::CastFromHand;
    a.card_name = D(name).card.m_name;
    a.def = &D(name);
    a.hand_index = hand_index;
    a.cost = D(name).card.m_mana_cost;
    return a;
}

Action Treasure()
{
    Action a;
    a.kind = Action::Kind::ActivatePermAbility;
    a.card_name = D("Heap Gate").card.m_name;
    a.def = &D("Heap Gate");
    a.ability_mode = Action::AbilityMode::TapTreasure;
    a.sac_source_id = 10;
    ManaCost one; one.generic = 1;
    a.cost = one;
    return a;
}
}   // namespace

TEST_CASE("Maze's End P5 (USER): Maze's End's fail-to-find is withheld while a Gate target is offered")
{
    LoadP5Cards();
    const MazesEndProvider prov;
    const CardDefinition& maze = D("Maze's End");
    GameState s = Base(prov);
    for (const char* g : { "Azorius Guildgate", "Boros Guildgate", "Dimir Guildgate" }) { Put(s, g, 500 + static_cast<int>(s.battlefield.size())); }
    std::vector<std::string> names = { "Azorius Guildgate", "Simic Guildgate" };
    {
        const Arm p3(heurarm::MAZE_LAND_TARGETS, 0);
        CHECK(prov.TapTutorPutCandidates(s, maze, names));          // narrowed: no fail-to-find
        CHECK(names == std::vector<std::string>{ "Simic Guildgate", "Azorius Guildgate" });
        names = { "Azorius Guildgate", "Simic Guildgate" };
        const Arm off(heurarm::MAZE_FIND_NOTHING, 0);
        CHECK_FALSE(prov.TapTutorPutCandidates(s, maze, names));    // the fail-to-find stays
    }
    // With the P3 menu on, the restricted menu also withholds it.
    names = { "Azorius Guildgate", "Simic Guildgate", "Gond Gate" };
    s.players[0].library.push_back(C("Gond Gate", 600));
    CHECK(prov.TapTutorPutCandidates(s, maze, names));
    CHECK_FALSE(names.empty());
}

TEST_CASE("Maze's End P5 (USER): Gateway Plaza is paid for or not played")
{
    LoadP5Cards();
    const MazesEndProvider prov;
    const CardDefinition& plaza = D("Gateway Plaza");
    GameState s = Base(prov);
    // The as-enters menu is {pay}.
    CHECK(prov.LandEntryPinCandidates(s, 0, plaza) == std::vector<int>{ 1 });
    {
        const Arm off(heurarm::MAZE_PLAZA_PAY, 0);
        CHECK(prov.LandEntryPinCandidates(s, 0, plaza) == std::vector<int>{ 1, 0 });
        CHECK(prov.LandPlayOffered(s, 0, plaza));
    }
    // No mana, no Amulet, no Gond Gate: not offered (its {1} cannot be paid on entry).
    CHECK_FALSE(prov.LandPlayOffered(s, 0, plaza));
    // Every other land is offered.
    CHECK(prov.LandPlayOffered(s, 0, D("Simic Guildgate")));
    // An untapped land pays it.
    GameState f = s;
    Put(f, "Forest", 20);
    CHECK(prov.LandPlayOffered(f, 0, plaza));
    // A TAPPED land does not.
    GameState t = s;
    Put(t, "Forest", 21, /*tapped=*/true);
    CHECK_FALSE(prov.LandPlayOffered(t, 0, plaza));
    // Amulet of Vigor untaps it (it pays with itself); so does a Gond Gate (a Gate enters untapped).
    GameState a = t;
    Put(a, "Amulet of Vigor", 22);
    CHECK(prov.LandPlayOffered(a, 0, plaza));
    GameState g = t;
    Put(g, "Gond Gate", 23, /*tapped=*/true);
    CHECK(prov.LandPlayOffered(g, 0, plaza));
    // Maze's End's fetch: the activation's {3} comes first. One untapped Forest cannot pay {3}+{1}: the Plaza is
    // dropped; with nothing else to find, the menu empties and the caller re-offers the fail-to-find.
    std::vector<std::string> names = { "Gateway Plaza" };
    {
        const Arm p3(heurarm::MAZE_LAND_TARGETS, 0);
        CHECK_FALSE(prov.TapTutorPutCandidates(f, D("Maze's End"), names));
        CHECK(names.empty());
        names = { "Gateway Plaza", "Simic Guildgate" };
        CHECK(prov.TapTutorPutCandidates(f, D("Maze's End"), names));
        CHECK(names == std::vector<std::string>{ "Simic Guildgate" });
        // Four untapped Forests pay {3} + {1}: kept.
        GameState four = f;
        for (int k = 0; k < 3; ++k) { Put(four, "Forest", 30 + k); }
        names = { "Gateway Plaza", "Simic Guildgate" };
        CHECK(prov.TapTutorPutCandidates(four, D("Maze's End"), names));
        CHECK(names.size() == 2);
    }
    // Burgeoning's put follows the land menu: a lone unpayable Plaza in hand is not put.
    GameState b = s;
    Put(b, "Burgeoning", 40);
    b.players[0].hand.push_back(C("Gateway Plaza", 41));
    ResolveBurgeoningTriggers(b);
    CHECK(b.players[0].hand.size() == 1);
    {
        const Arm off(heurarm::MAZE_PLAZA_PAY, 0);
        GameState b2 = s;
        Put(b2, "Burgeoning", 40);
        b2.players[0].hand.push_back(C("Gateway Plaza", 41));
        ResolveBurgeoningTriggers(b2);
        CHECK(b2.players[0].hand.empty());   // put, then sacrificed (unaffordable)
    }
}

TEST_CASE("Maze's End P5 (USER): Manabond's no is withheld for an all-land hand, unless a Plaza cannot pay")
{
    LoadP5Cards();
    const MazesEndProvider prov;
    GameState s = Base(prov);
    s.phase = Phase::Ending;
    Put(s, "Manabond", 700);
    s.players[0].hand.push_back(C("Simic Guildgate", 710));
    s.players[0].hand.push_back(C("Forest", 711));
    CHECK(prov.CrossingDumpAllLandsDominatesNo(s, 0));
    // The searched "no" (rank 1) is gone: the dump happens and no rank is consumed.
    {
        GameState t = s;
        t.scripted_crossing_lands = 1;
        PerformEndStepManabond(t);
        CHECK(t.players[0].hand.empty());
        CHECK(t.scripted_crossing_lands == 1);
    }
    {
        const Arm off(heurarm::MAZE_MANABOND_ALL_LANDS, 0);
        CHECK_FALSE(prov.CrossingDumpAllLandsDominatesNo(s, 0));
        GameState t = s;
        t.scripted_crossing_lands = 1;
        PerformEndStepManabond(t);
        CHECK(t.players[0].hand.size() == 2);   // the searched no
    }
    // A Gateway Plaza that cannot pay at the end step keeps "no".
    GameState p = s;
    p.players[0].hand.push_back(C("Gateway Plaza", 712));
    CHECK_FALSE(prov.CrossingDumpAllLandsDominatesNo(p, 0));
    // ...but with an untapped land to pay it, "no" is pruned again.
    Put(p, "Forest", 713);
    CHECK(prov.CrossingDumpAllLandsDominatesNo(p, 0));
}

TEST_CASE("Maze's End P5 (USER): Heap Gate's Treasure plan only where its mana has no other use")
{
    LoadP5Cards();
    const MazesEndProvider prov;
    const Arm strict_off(heurarm::MAZE_HEAP_STRICT, 0);   // P5's leftover-or-binds rule (the strict rule ships ON)
    GameState s = Base(prov);
    Put(s, "Heap Gate", 10);
    Put(s, "Simic Guildgate", 11);
    for (int k = 0; k < 4; ++k) { Put(s, "Forest", 12 + k); }   // 7 lands: next turn's mana does not bind
    s.players[0].hand.push_back(C("Exploration", 20));
    CHECK(prov.PlanListFilterLive(s));
    const std::string none, land = "Forest";
    const std::vector<Action> treasure_only = { Treasure() };
    const std::vector<Action> cast_only     = { Cast("Exploration", 0) };
    const std::vector<Action> both          = { Cast("Exploration", 0), Treasure() };
    const std::vector<Action> empty;
    // The menu holds {Exploration}: the Treasure alone took mana the cast could use -> removed; the plan that casts
    // AND makes the Treasure stays (nothing else to spend on), as do the Treasure-free plans.
    std::vector<DecisionProvider::PlanView> v = {
        { &empty, &none }, { &treasure_only, &none }, { &cast_only, &none }, { &both, &none } };
    std::vector<char> admit(v.size(), 1);
    prov.PlanListAdmit(s, v, admit);
    CHECK(admit == std::vector<char>{ 1, 0, 1, 1 });
    // Lever off: not live.
    {
        const Arm off(heurarm::MAZE_HEAP_TREASURE, 0);
        CHECK_FALSE(prov.PlanListFilterLive(s));
    }
    // Next turn's mana binds (a held Talon Gates' {4} with three lands and none in hand): the Treasure is kept.
    GameState b = Base(prov);
    Put(b, "Heap Gate", 10);
    Put(b, "Simic Guildgate", 11);
    b.players[0].hand.push_back(C("Talon Gates of Madara", 21));
    b.players[0].hand.push_back(C("Exploration", 20));
    std::vector<char> admit2(v.size(), 1);
    prov.PlanListAdmit(b, v, admit2);
    CHECK(admit2 == std::vector<char>{ 1, 1, 1, 1 });
}

TEST_CASE("Maze's End P5: the v5 fetch weight, P1's one colour and the Talon variant")
{
    LoadP5Cards();
    const MazesEndProvider prov;
    GameState s = Base(prov);
    s.players[0].library.push_back(C("Guild Summit", 100));
    s.players[0].library.push_back(C("Sylvan Scrying", 101));
    Put(s, "Azorius Guildgate", 50);
    // v5: a new-name fetch scores 100 (no land drop open).
    s.players[0].lands_played_this_turn = 1;
    CHECK(prov.TapTutorPutEval(s, D("Maze's End"), "Simic Guildgate") == 100);
    {
        const Arm off(heurarm::MAZE_FETCH_EVAL_V5, 0);
        CHECK(prov.TapTutorPutEval(s, D("Maze's End"), "Simic Guildgate") == 200);
    }
    // P1's one colour: Azorius (W/U) is a blue source, no green -> G.
    const CardDefinition& cliff = D("Cliffgate");
    CHECK(prov.LandEntryPinCandidates(s, 0, cliff) == std::vector<int>{ static_cast<int>(Color::Green) });
    GameState g2 = s;
    Put(g2, "Forest", 51);
    Put(g2, "Forest", 52);
    // P1: green and blue sources -> G.
    CHECK(prov.LandEntryPinCandidates(g2, 0, cliff) == std::vector<int>{ static_cast<int>(Color::Green) });
    // Talon variant: with an Exploration controlled, a land tutor no longer offers Talon Gates.
    GameState t = Base(prov);
    Put(t, "Maze's End", 60);
    Put(t, "Gond Gate", 61);
    t.players[0].library.push_back(C("Talon Gates of Madara", 62));
    t.players[0].library.push_back(C("Baldur's Gate", 63));   // in the library: no new-name slot on the P3 menu
    t.players[0].library.push_back(C("Maze's End", 64));
    const CardParams& scry = D("Sylvan Scrying").params;
    auto has_talon = [&](const GameState& st)
    {
        const std::vector<std::string> c = prov.TutorCandidates(st, 0, scry);
        return std::find(c.begin(), c.end(), "Talon Gates of Madara") != c.end();
    };
    CHECK(has_talon(t));
    Put(t, "Exploration", 65);
    CHECK(has_talon(t));
    const Arm lack(heurarm::MAZE_TALON_LACKDROPS, 1);
    CHECK_FALSE(has_talon(t));
}

TEST_CASE("Maze's End P5 (USER): the strict Treasure rule -- never a branch, made at the last chance")
{
    // MTG_MAZE_HEAP_STRICT (USER: "there is no drawback to doing so"; "We can actually create the treasure at the very
    // end of processing even after Maze's End"): no Treasure plan on the menu; PerformLastChanceTreasure makes it
    // after the opponent's turn whenever the untapped sources still pay {1} + {T} Heap Gate + tap another Gate.
    LoadP5Cards();
    const MazesEndProvider prov;
    auto treasures = [](const GameState& g)
    {
        int n = 0;
        for (const Permanent& p : g.battlefield) { if (p.is_token && p.controller_index == 0) { ++n; } }
        return n;
    };
    GameState s = Base(prov);
    Put(s, "Heap Gate", 10);
    Put(s, "Planar Nexus", 11);   // a Gate (every nonbasic type)
    Put(s, "Forest", 12);
    // Lever off: nothing happens (byte-identical).
    {
        const Arm off(heurarm::MAZE_HEAP_STRICT, 0);
        GameState g = s;
        CHECK(PerformLastChanceTreasure(g) == 0);
        CHECK(treasures(g) == 0);
    }
    const Arm strict(heurarm::MAZE_HEAP_STRICT, 1);
    // Every Treasure plan leaves the menu (the Treasure-free plans stay).
    {
        const std::string none;
        const std::vector<Action> empty;
        const std::vector<Action> treasure_only = { Treasure() };
        const std::vector<Action> cast_only = { Cast("Exploration", 0) };
        const std::vector<Action> both = { Cast("Exploration", 0), Treasure() };
        std::vector<DecisionProvider::PlanView> v = {
            { &empty, &none }, { &treasure_only, &none }, { &cast_only, &none }, { &both, &none } };
        std::vector<char> admit(v.size(), 1);
        REQUIRE(prov.PlanListFilterLive(s));
        prov.PlanListAdmit(s, v, admit);
        CHECK(admit == std::vector<char>{ 1, 0, 1, 0 });
    }
    // Two spare sources besides Heap Gate's own tap (the Nexus as the tapped Gate, the Forest's {1}) after
    // everything: the Treasure is made, once, and every source it used is tapped.
    {
        GameState g = s;
        CHECK(PerformLastChanceTreasure(g) == 1);
        CHECK(treasures(g) == 1);
        int untapped_lands = 0;
        for (const Permanent& p : g.battlefield) { if (p.card.IsLand() && !p.tapped) { ++untapped_lands; } }
        CHECK(untapped_lands == 0);
    }
    // A plan that spent its mana (the Forest tapped): no {1} -> no Treasure.
    {
        GameState g = s;
        for (Permanent& p : g.battlefield) { if (p.card.m_number == 12) { p.tapped = true; } }
        CHECK(PerformLastChanceTreasure(g) == 0);
        CHECK(treasures(g) == 0);
    }
    // No other untapped Gate to tap: no Treasure.
    {
        GameState g = s;
        for (Permanent& p : g.battlefield) { if (p.card.m_number == 11) { p.tapped = true; } }
        CHECK(PerformLastChanceTreasure(g) == 0);
    }
    // An off-main Maze's End activation fires first (it wins at the end step after Manabond's put) and leaves too
    // little: no Treasure.
    {
        GameState g = s;
        Put(g, "Maze's End", 1);
        Put(g, "Forest", 13);
        int n = 30;
        for (const char* nm : { "Azorius Guildgate", "Boros Guildgate", "Dimir Guildgate", "Golgari Guildgate",
                                "Gruul Guildgate", "Izzet Guildgate", "Orzhov Guildgate", "Rakdos Guildgate" })
        { Put(g, nm, n++, /*tapped=*/true); }
        g.players[0].library.push_back(C("Simic Guildgate", 50));
        g.players[0].library.push_back(C("Selesnya Guildgate", 51));
        REQUIRE(TryOffMainAltWinActivation(g));
        CHECK(PerformLastChanceTreasure(g) == 0);
        CHECK(treasures(g) == 0);
    }
}

TEST_CASE("Maze's End P5 (USER): the three readings of \"lacking extra land drops\" for Talon Gates")
{
    LoadP5Cards();
    const MazesEndProvider prov;
    const CardParams& scry = D("Sylvan Scrying").params;
    auto has_talon = [&](const GameState& st)
    {
        const std::vector<std::string> c = prov.TutorCandidates(st, 0, scry);
        return std::find(c.begin(), c.end(), "Talon Gates of Madara") != c.end();
    };
    GameState t = Base(prov);
    Put(t, "Maze's End", 60);
    Put(t, "Gond Gate", 61);
    t.players[0].library.push_back(C("Talon Gates of Madara", 62));
    t.players[0].library.push_back(C("Baldur's Gate", 63));
    t.players[0].library.push_back(C("Maze's End", 64));
    // One Burgeoning, no land in hand: (a) no, (b) yes (one copy), (c) yes (it cannot put anything).
    GameState b = t;
    Put(b, "Burgeoning", 65);
    CHECK(has_talon(b));                                   // the always-offered amendment
    { const Arm a(heurarm::MAZE_TALON_LACKDROPS, 1); CHECK_FALSE(has_talon(b)); }
    { const Arm a(heurarm::MAZE_TALON_ATMOST1, 1);   CHECK(has_talon(b)); }
    { const Arm a(heurarm::MAZE_TALON_EFFECTIVE, 1); CHECK(has_talon(b)); }
    // One land in hand with the drop still open: not spare -> (c) still offers it.
    GameState b1 = b;
    b1.players[0].lands_played_this_turn = 0;
    b1.players[0].hand.push_back(C("Forest", 66));
    { const Arm a(heurarm::MAZE_TALON_EFFECTIVE, 1); CHECK(has_talon(b1)); }
    // A second land: spare -> Burgeoning is effective -> (c) withholds Talon.
    GameState b2 = b1;
    b2.players[0].hand.push_back(C("Forest", 67));
    { const Arm a(heurarm::MAZE_TALON_EFFECTIVE, 1); CHECK_FALSE(has_talon(b2)); }
    // Burgeoning + a Manabond in hand: two copies -> (b) withholds.
    GameState two = b;
    two.players[0].hand.push_back(C("Manabond", 68));
    { const Arm a(heurarm::MAZE_TALON_ATMOST1, 1); CHECK_FALSE(has_talon(two)); }
    // Exploration always counts for (c).
    GameState x = t;
    Put(x, "Exploration", 69);
    { const Arm a(heurarm::MAZE_TALON_EFFECTIVE, 1); CHECK_FALSE(has_talon(x)); }
}

TEST_CASE("Maze's End F5: the strict Treasure rule reaches every enumeration (Heap Gate from hand; the main-2 host)")
{
    // d5 s171015 gi14 T3 committed [Guild Summit, Heap Gate(act)] with Heap Gate PLAYED FROM HAND: the filter's
    // liveness read only the battlefield, so a land-drop Heap Gate's Treasure plan stayed on the menu. And the
    // main-2 host (EnumerateM2PlansBody) never applied the provider's plan-list restriction at all.
    LoadP5Cards();
    const MazesEndProvider prov;
    auto with_t = [](const std::vector<TurnSolver::Plan>& plans)
    {
        int n = 0;
        for (const TurnSolver::Plan& p : plans)
        {
            for (const Action& a : p.actions)
            {
                if (a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::TapTreasure)
                { ++n; break; }
            }
        }
        return n;
    };
    // Main 1: Heap Gate in hand, the drop open, another untapped Gate and a Forest to pay {1} + the Gate tap.
    GameState m1 = Base(prov);
    m1.turn_number = 3;
    Put(m1, "Simic Guildgate", 11);
    Put(m1, "Forest", 12);
    m1.players[0].hand.push_back(C("Heap Gate", 10));
    m1.players[0].library.push_back(C("Forest", 40));
    m1.players[0].library.push_back(C("Forest", 41));
    {
        const Arm off(heurarm::MAZE_HEAP_STRICT, 0);   // the P5 rule: nothing else to spend on -> offered
        CHECK(prov.PlanListFilterLive(m1));
        CHECK(with_t(TurnSolver::EnumerateMainPlans(m1, true)) > 0);
    }
    {
        const Arm on(heurarm::MAZE_HEAP_STRICT, 1);
        CHECK(prov.PlanListFilterLive(m1));
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(m1, true);
        CHECK_FALSE(plans.empty());
        CHECK(with_t(plans) == 0);
    }
    // Main 2 (the search's main-2 host): Heap Gate untapped on the battlefield with the same spare sources.
    GameState m2 = Base(prov);
    m2.phase = Phase::PostCombatMain;
    Put(m2, "Heap Gate", 10);
    Put(m2, "Simic Guildgate", 11);
    Put(m2, "Forest", 12);
    m2.players[0].lands_played_this_turn = 1;
    m2.players[0].library.push_back(C("Forest", 40));
    {
        const Arm off(heurarm::MAZE_HEAP_STRICT, 0);
        CHECK(with_t(TurnSolver::EnumerateM2HostPlansForTest(m2)) > 0);
    }
    {
        const Arm on(heurarm::MAZE_HEAP_STRICT, 1);
        // The host's only plans held the Treasure: the list EMPTIES (the caller appends the do-nothing plan). The
        // old "never empties" keep returned it whole -- the Treasure was still a branch against the pass.
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateM2HostPlansForTest(m2);
        CHECK(with_t(plans) == 0);
    }
}
